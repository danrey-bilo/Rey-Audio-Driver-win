#include "commands.hpp"
#include "../engine/transport_tuning.hpp"
#include <sstream>
namespace rey::service {
bool command_settings(const Settings &current, const std::string &command, Settings &next, std::string &error) {
  std::istringstream input(command);
  std::vector<std::string> fields;
  for (std::string f; input >> f;)
    fields.push_back(f);
  next = current;
  bool accepted = false;
  if (fields.size() == 7 && fields[0] == "MIX") {
    unsigned direction = 0, channel = 0, biased_gain = 0, mute = 0, solo = 0, phase = 0;
    const auto split = fields[1].find(':');
    accepted = split != std::string::npos && parse_unsigned(fields[1].substr(0, split), direction, 1) &&
      parse_unsigned(fields[1].substr(split + 1), channel, 7) && parse_unsigned(fields[2], biased_gain, 7200) &&
      parse_unsigned(fields[3], mute, 1) && parse_unsigned(fields[4], solo, 1) && parse_unsigned(fields[5], phase, 1) && fields[6] == "apply";
    if (accepted) {
      auto &group = direction == 0 ? next.mix.inputs : next.mix.outputs;
      auto &c = group[channel];
      c.gain_cdb = int(biased_gain) - 6000; c.mute = mute != 0; c.solo = solo != 0; c.invert = phase != 0;
      if (next.mix.stereo_links[direction] & (1u << (channel / 2))) group[channel ^ 1u] = c;
    }
  } else if (fields.size() == 8 && fields[0] == "LINK") {
    unsigned direction = 0, pair = 0, linked = 0, gain = 0, mute = 0, solo = 0, phase = 0;
    const auto split = fields[1].find(':');
    accepted = split != std::string::npos && parse_unsigned(fields[1].substr(0, split), direction, 1) &&
      parse_unsigned(fields[1].substr(split + 1), pair, 3) && parse_unsigned(fields[2], linked, 1) &&
      parse_unsigned(fields[3], gain, 7200) && parse_unsigned(fields[4], mute, 1) &&
      parse_unsigned(fields[5], solo, 1) && parse_unsigned(fields[6], phase, 1) && fields[7] == "apply";
    if (accepted) {
      auto &mask = next.mix.stereo_links[direction];
      if (linked) {
        mask |= uint8_t(1u << pair);
        auto &group = direction == 0 ? next.mix.inputs : next.mix.outputs;
        group[pair * 2] = {int(gain) - 6000, mute != 0, solo != 0, phase != 0};
        group[pair * 2 + 1] = group[pair * 2];
      } else mask &= uint8_t(~(1u << pair));
    }
  } else if (fields.size() == 3 && fields[0] == "MASTER") {
    unsigned gain = 0, mute = 0;
    accepted = parse_unsigned(fields[1], gain, 6600) && parse_unsigned(fields[2], mute, 1);
    next.mix.master_cdb = int(gain) - 6000; next.mix.master_mute = mute != 0;
  } else if (fields.size() == 2 && fields[0] == "BOARD_MODE") {
    accepted = fields[1] == "soundcard" || fields[1] == "combined";
    next.board_effects = fields[1] == "combined";
    if (next.board_effects && next.usb.rate > 96000) next.usb.rate = 96000;
  } else if (command == "MIX_RESET") {
    const auto links = next.mix.stereo_links;
    next.mix = rey::audio::Mix{}; next.mix.stereo_links = links; accepted = true;
  } else if (fields.size() == 3 && fields[0] == "FORMAT") {
    unsigned rate=0,bits=0;
    accepted=parse_unsigned(fields[1],rate,192000) && parse_unsigned(fields[2],bits,32);
    next.usb.rate=rate;next.usb.bits=uint16_t(bits);
    next.usb_depth=rey::engine::transport_tuning(rate).requests;
    next.usb.safety=0;next.usb_auto=true;
  } else if (fields.size() == 7 && fields[0] == "USB") {
    unsigned rate = 0, bits = 0, depth = 0, block = 0, guard = 0, automatic = 0;
    accepted = parse_unsigned(fields[1], rate, 192000) &&
               parse_unsigned(fields[2], bits, 32) &&
               parse_unsigned(fields[3], depth, 16) &&
               parse_unsigned(fields[4], block, 256) &&
               parse_unsigned(fields[5], guard, 8192) &&
               parse_unsigned(fields[6], automatic, 1);
    next.usb.rate = rate;
    next.usb.bits = uint16_t(bits);
    next.usb_depth = depth;
    next.usb.block = block;
    next.usb.safety = guard;
    next.usb_auto = automatic != 0;
  }
  if (!accepted)
    { error = "Unknown or malformed command"; return false; }
  return valid(next, error);
}
}
