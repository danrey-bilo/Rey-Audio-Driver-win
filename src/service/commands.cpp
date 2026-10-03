#include "commands.hpp"
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
      auto &c = direction == 0 ? next.mix.inputs[channel] : next.mix.outputs[channel];
      c.gain_cdb = int(biased_gain) - 6000; c.mute = mute != 0; c.solo = solo != 0; c.invert = phase != 0;
    }
  } else if (fields.size() == 3 && fields[0] == "MASTER") {
    unsigned gain = 0, mute = 0;
    accepted = parse_unsigned(fields[1], gain, 6600) && parse_unsigned(fields[2], mute, 1);
    next.mix.master_cdb = int(gain) - 6000; next.mix.master_mute = mute != 0;
  } else if (command == "MIX_RESET") {
    next.mix = rey::audio::Mix{}; accepted = true;
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
  } else if (fields.size() == 7 && fields[0] == "LAN") {
    unsigned port = 0, block = 0, guard = 0, frames = 0, energy = 0;
    accepted = fields[1].size() < sizeof(next.lan.peer) &&
               parse_unsigned(fields[2], port, 65535) &&
               parse_unsigned(fields[3], block, 256) &&
               parse_unsigned(fields[4], guard, 8192) &&
               parse_unsigned(fields[5], frames, 256) &&
               parse_unsigned(fields[6], energy, 1);
    std::snprintf(next.lan.peer, sizeof(next.lan.peer), "%s",
                  fields[1].c_str());
    next.lan.port = uint16_t(port);
    next.lan.block = block;
    next.lan.safety = guard;
    next.lan.capture_frames = frames;
    next.lan.energy_saving = energy != 0;
  } else if (command == "LAN_CONNECT") {
    next.lan_enabled = true;
    next.preferred = "lan";
    accepted = true;
  } else if (command == "LAN_DISCONNECT") {
    next.lan_enabled = false;
    next.preferred = "auto";
    accepted = true;
  } else if (fields.size() == 2 && fields[0] == "USE") {
    next.preferred = fields[1];
    if (fields[1] == "usb")
      next.usb_auto = true;
    accepted = true;
  }
  if (!accepted)
    { error = "Unknown or malformed command"; return false; }
  return valid(next, error);
}
}
