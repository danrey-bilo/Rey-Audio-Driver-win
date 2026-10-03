#include "peer_session.hpp"
#include "aoip/device_identity.hpp"
namespace piaoip::engine {
bool PeerSession::prepare(Config &config, std::string &error) {
  if (!config.peer[0] || std::strcmp(config.peer, "auto") == 0) {
    SettingsDialog discovery;
    discover_devices(discovery);
    if (discovery.devices.size() != 1) {
      error = "Select one wired Pi in the configuration";
      return false;
    }
    std::snprintf(config.peer, sizeof(config.peer), "%s", discovery.devices.front().ip);
  }
  SettingsDialog::Device source;
  if (!query_device(config.peer, source) || !source.v3) {
    error = "A V3 peer is required";
    return false;
  }
  if (config.block < 16 || config.block > 256 || !aoip::valid_buffer(unsigned(config.block)) ||
      config.safety > 8192 || source.channels > 8 || source.outputs > 8 ||
      config.audio_spin_us > 80) {
    error = "Single-Pi bridge supports up to 8 channels and manual blocks 16..256";
    return false;
  }
  config.rate = source.rate;
  config.bits = uint16_t(source.bits);
  config.channels = uint16_t(source.channels);
  config.inputs = source.channels;
  config.outputs = source.outputs;
  config.peer_port = uint16_t(source.port);
  inputs_.assign(config.input_mask, source.channels);
  outputs_.assign(config.output_mask, source.outputs);
  if (!inputs_.count && !outputs_.count) {
    error = "Select at least one input or output";
    return false;
  }
  capture_frames_ =
      aoip::select_capture_frames(std::max(1u, inputs_.count), config.rate, config.bits,
                                  unsigned(config.block), source.frames, config.capture_frames);
  budget_ = aoip::transport_budget(
      {inputs_.count, outputs_.count, config.rate, config.bits, unsigned(config.block)},
      capture_frames_, aoip::WireProtocol::v3, source.link_mbps, source.max_pps);
  if (!capture_frames_ || !budget_.fits) {
    error = "Explicit packet/block profile exceeds MTU or wire budget";
    return false;
  }
  char reply[512]{}, origin[32]{};
  sockaddr_in responder{};
  int end = 0;
  if (!control_request(config.peer, nullptr, "PIAOIP_IDENTITY_V1", reply, responder, 100) ||
      std::sscanf(reply, "PIAOIP_IDENTITY_V1 id=%32s origin=%31s backend=%31s%n", identity_, origin,
                  backend_, &end) != 3 ||
      end != int(std::strlen(reply)) || !aoip::peer::valid_device_id(identity_)) {
    error = "Peer has no stable identity; update the Pi runtime";
    return false;
  }
  token_ = uint32_t((now_ns() >> 4) ^ GetCurrentProcessId()) | 1u;
  return true;
}
bool PeerSession::subscribe(const Config &config, std::string &error) {
  char request[256]{}, reply[512]{};
  sockaddr_in responder{};
  std::snprintf(request, sizeof(request),
                "PIAOIP_SUBSCRIBE_V3 port=%u channels=%u rate=%u bits=%u outputs=%u "
                "in_mask=%016llx out_mask=%016llx frames=%u session=%u energy=%u",
                config.port, config.channels, config.rate, config.bits, config.outputs,
                static_cast<unsigned long long>(inputs_.mask),
                static_cast<unsigned long long>(outputs_.mask), capture_frames_, token_,
                config.energy_saving ? 1 : 0);
  for (unsigned attempt = 0; attempt < 3; ++attempt) {
    if (!control_request(config.peer, nullptr, request, reply, responder, 100))
      continue;
    unsigned token = 0, epoch = 0, inputs = 0, outputs = 0, frames = 0;
    int end = 0;
    if (std::sscanf(reply,
                    "PIAOIP_SUBSCRIBED_V3 session=%u epoch=%u inputs=%u outputs=%u frames=%u%n",
                    &token, &epoch, &inputs, &outputs, &frames, &end) == 5 &&
        end == int(std::strlen(reply)) && token == token_ && epoch > 0 && epoch <= 65535 &&
        inputs == inputs_.count && outputs == outputs_.count && frames == capture_frames_) {
      epoch_ = uint16_t(epoch);
      subscribed_ = true;
      return true;
    }
    error = reply;
    return false;
  }
  // A lost acknowledgement can still leave our lease active. Release only our
  // token, even when the successful acknowledgement never reached Windows.
  unsubscribe(config);
  error = "Subscription acknowledgement timed out";
  return false;
}
bool PeerSession::keepalive(const Config &config) {
  char request[96]{}, reply[512]{};
  sockaddr_in responder{};
  std::snprintf(request, sizeof(request), "PIAOIP_KEEPALIVE_V3 session=%u", token_);
  unsigned token = 0, source_silent = 0, sink_silent = 0;
  int end = 0;
  return control_request(config.peer, nullptr, request, reply, responder, 100) &&
         std::sscanf(reply, "PIAOIP_ALIVE_V3 session=%u source_silent=%u sink_silent=%u%n", &token,
                     &source_silent, &sink_silent, &end) == 3 &&
         end == int(std::strlen(reply)) && token == token_ && source_silent <= 1 &&
         sink_silent <= 1;
}
void PeerSession::unsubscribe(const Config &config) {
  if (!token_)
    return;
  char request[96]{}, reply[512]{};
  sockaddr_in responder{};
  std::snprintf(request, sizeof(request), "PIAOIP_UNSUBSCRIBE_V3 session=%u", token_);
  for (unsigned attempt = 0; attempt < 2; ++attempt)
    if (control_request(config.peer, nullptr, request, reply, responder, 100))
      break;
  subscribed_ = false;
  token_ = 0;
}
} // namespace piaoip::engine
