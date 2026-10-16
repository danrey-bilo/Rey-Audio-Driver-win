#include "../src/engine/capture_batch.hpp"
#include <array>
#include <stdexcept>
struct Host {
  unsigned captured = 0, rendered = 0, expected = 0, fail_at = 99;
  static bool process(void *opaque, const rey::engine::AudioBlock &block) {
    auto &host = *static_cast<Host *>(opaque);
    if (block.capture_only && !block.render_only) {
      if (host.rendered || block.frame_position != host.captured * 6)
        throw std::runtime_error("capture order");
      if (host.captured == host.fail_at)
        return false;
      ++host.captured;
    } else if (block.render_only && !block.capture_only) {
      if (host.captured != host.expected ||
          block.frame_position != host.rendered * 6)
        throw std::runtime_error(
            "render requested before completed input was published");
      for (unsigned i = 0; i < block.frames * 8; ++i)
        block.render[i] = block.capture[i] * 2;
      ++host.rendered;
    } else
      throw std::runtime_error("unsplit fragment");
    return true;
  }
};
int main() {
  for (unsigned count = 1; count <= 16; ++count) {
    std::array<std::array<int32_t, 48>, 16> input{}, output{};
    std::array<rey::engine::AudioBlock, 16> blocks{};
    for (unsigned n = 0; n < count; ++n) {
      input[n].fill(int32_t(n + 1));
      blocks[n] = {input[n].data(), output[n].data(), 6, 8, 8, 32};
      blocks[n].frame_position = n * 6;
      blocks[n].scheduled_ns = 1000 + n * 125000;
    }
    Host host;
    host.expected = count;
    if (!rey::engine::publish_capture_batch(Host::process, &host, blocks.data(),
                                            count))
      return 1;
    for (unsigned n = 0; n < count; ++n) {
      if (!rey::engine::render_captured_block(Host::process, &host, blocks[n]))
        return 2;
      if (output[n][47] != int32_t(2 * (n + 1)) || blocks[n].capture_only ||
          blocks[n].render_only)
        return 3;
    }
    Host aborted;
    aborted.expected = count;
    aborted.fail_at = 0;
    if (rey::engine::publish_capture_batch(Host::process, &aborted,
                                           blocks.data(), count) ||
        aborted.rendered)
      return 4;
  }
  return 0;
}
