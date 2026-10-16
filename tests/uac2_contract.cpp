#include "../src/engine/uac2_pcm.hpp"
#include <array>
#include <iostream>
#include <limits>
using namespace rey::engine;
int main() {
  bool ok = true;
  for (unsigned bits : {16u,24u,32u}) {
    const auto full = int64_t(1) << (bits-1);
    for (auto value : {-full,-full+1,int64_t(-1),int64_t(0),int64_t(1),full-1})
      ok &= uac2_decode(uac2_encode(int32_t(value), bits), bits) == value;
    ok &= uac2_decode(-1, bits) == -1;
    Uac2Queue queue(9);
    std::array<int32_t, 8*8> input{}, output{};
    for (unsigned round = 0; round < 100; ++round) {
      for (unsigned f = 0; f < 8; ++f) for (unsigned ch = 0; ch < 8; ++ch)
        input[f*8+ch] = int32_t(f*8+ch+round*64)-3200;
      ok &= queue.push(input.data(), 8, bits) == 0;
      ok &= queue.pop(reinterpret_cast<uint8_t *>(output.data()), 3) == 3;
      ok &= queue.pop(reinterpret_cast<uint8_t *>(output.data()+3*8), 5) == 5;
      for (unsigned n = 0; n < 64; ++n) ok &= uac2_decode(output[n], bits) == input[n];
    }
    ok &= queue.pop(reinterpret_cast<uint8_t *>(output.data()), 8) == 0;
    for (auto sample : output) ok &= sample == 0;
    // Keep the newest samples on overflow and preserve all eight channel positions.
    for (unsigned n = 0; n < input.size(); ++n) input[n] = int32_t(n);
    Uac2Queue small(3);
    ok &= small.push(input.data(), 8, bits) == 5;
    ok &= small.pop(reinterpret_cast<uint8_t *>(output.data()), 4) == 3;
    for (unsigned n = 0; n < 24; ++n) ok &= uac2_decode(output[n], bits) == input[40+n];
    for (unsigned n = 24; n < 32; ++n) ok &= output[n] == 0;
  }
  std::cout << "UAC2 signed PCM, channel mapping, queue wrap, silence, overflow " << (ok ? "PASS\n" : "FAIL\n");
  return ok ? 0 : 1;
}
