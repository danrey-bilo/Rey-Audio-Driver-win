#include "../include/rey/endpoints.h"
#include "../include/rey/bridge.h"
#include <array>
#include <cstring>
#include <iostream>
#include <limits>
static bool check(bool ok, const char *what) { if (!ok) std::cerr << what << '\n'; return ok; }
int main(int argc, char **argv) {
  if (argc != 2) return 1;
  const std::string test(argv[1]); bool ok = true;
  if (test == "pairs") {
    ok &= check(REY_ENDPOINT_SLOTS == 8, "Exactly four input and four output pairs");
    for (unsigned slot = 0; slot < REY_ENDPOINT_SLOTS; ++slot) {
      ok &= check(rey_endpoint_channels(slot, 8, 8) == 2, "Stereo endpoint channels");
      ok &= check(rey_endpoint_first_channel(slot) == (slot / 2) * 2, "pair mapping");
    }
    ok &= check(rey_endpoint_channels(6, 7, 8) == 1 && rey_endpoint_channels(6, 3, 0) == 0 &&
        rey_endpoint_channels(8, 8, 8) == 0, "odd channel tail and absent pins");
  } else if (test == "phases") {
    REY_BRIDGE_TIMELINE clock{};
    REY_BRIDGE_PROFILE profile{sizeof(REY_BRIDGE_PROFILE),REY_BRIDGE_VERSION,96000,32,8,8,64,0,{}};
    std::memset(profile.device_id,'1',32);
    REY_BRIDGE_EXCHANGE packet{};
    packet.size=sizeof(packet);packet.version=REY_BRIDGE_VERSION;packet.frames=12;
    packet.capture_channels=packet.render_channels=8;packet.epoch=1;
    unsigned gap[2]{};
    for (unsigned frame : {0u,12u,24u}) for (unsigned phase : {REY_BRIDGE_CAPTURE_ONLY,REY_BRIDGE_RENDER_ONLY}) {
      packet.frame_position=frame;packet.flags=phase;
      ok &= check(rey_bridge_valid_exchange(&packet,&profile) && rey_bridge_advance(&clock,&packet,256,gap),"Separate capture/render callbacks share the same frame position");
      ok &= check(!gap[0] && !gap[1],"No duplicate frame advancement");
    }
    packet.flags=REY_BRIDGE_CAPTURE_ONLY|REY_BRIDGE_RENDER_ONLY;
    ok &= check(!rey_bridge_valid_exchange(&packet,&profile),"Contradictory phase flags rejected");
    packet.flags=REY_BRIDGE_CAPTURE_ONLY;
    ok &= check(!rey_bridge_advance(&clock,&packet,256,gap),"Duplicate capture cannot rewind the stream");
    packet.frame_position=48;packet.flags|=REY_BRIDGE_DISCONTINUITY;
    ok &= check(rey_bridge_advance(&clock,&packet,256,gap) && gap[0]==12 && clock.next[1]==36,"Capture gaps do not advance playback");
  } else if (test == "capture") {
    std::array<int32_t, 37 * 8> source{};
    for (unsigned f = 0; f < 37; ++f) for (unsigned c = 0; c < 8; ++c) source[f * 8 + c] = int(f * 100 + c);
    for (unsigned pair = 0; pair < 4; ++pair) {
      std::array<int32_t, 37 * 2 + 2> result; result.fill(-999);
      rey_endpoint_capture_pcm(result.data() + 1, source.data(), 37, 2, 8, pair * 2);
      for (unsigned f = 0; f < 37; ++f) for (unsigned c = 0; c < 2; ++c)
        ok &= check(result[1 + f * 2 + c] == int(f * 100 + pair * 2 + c), "strided capture PCM");
      ok &= check(result.front() == -999 && result.back() == -999, "capture canaries");
      rey_endpoint_capture_pcm(result.data() + 1, nullptr, 37, 2, 8, pair * 2);
      for (unsigned i = 1; i < result.size() - 1; ++i) ok &= check(result[i] == 0, "capture gap must be silence");
    }
  } else if (test == "render") {
    std::array<int32_t, 17 * 8> output{}, full{}; full.fill(100);
    rey_endpoint_render_pcm(output.data(), full.data(), 17, 8, 8, 0, 32);
    for (unsigned pair = 0; pair < 4; ++pair) {
      std::array<int32_t, 17 * 2> input; input.fill(1000 + pair);
      rey_endpoint_render_pcm(output.data(), input.data(), 17, 2, 8, pair * 2, 32);
    }
    for (unsigned f = 0; f < 17; ++f) for (unsigned c = 0; c < 8; ++c)
      ok &= check(output[f * 8 + c] == int(1100 + c / 2), "render pairs combine with full endpoint without crossover");
  } else if (test == "clipping") {
    for (const auto bits : {16u, 24u, 32u}) {
      const int32_t maximum = int32_t((uint64_t(0x7fffffff) >> (32 - bits)) << (32 - bits));
      const int32_t minimum = std::numeric_limits<int32_t>::min();
      int32_t output[4] = {maximum, minimum, 77, 88}, input[2] = {maximum, minimum};
      ok &= check(rey_endpoint_render_pcm(output, input, 1, 2, 4, 0, bits) == 2, "clipping count");
      ok &= check(output[0] == maximum && output[1] == minimum && output[2] == 77 && output[3] == 88,
                  "PCM saturation preserves valid-bit alignment and unrelated channels");
    }
  } else return 1;
  return ok ? 0 : 2;
}
