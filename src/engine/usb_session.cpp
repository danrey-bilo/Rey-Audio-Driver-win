#include "usb_session.hpp"
#include "capture_batch.hpp"
#include "pi5ausb/pcm.hpp"
#include <array>
namespace rey::engine {
bool UsbSession::open(const UsbProfile &cfg, const std::string &expected_id,
                      std::string &error) {
  profile_ = {cfg.rate, std::uint8_t(cfg.bits), 8, 8};
  if (!pi5ausb::valid_profile(profile_) || cfg.bits > 32 ||
      (cfg.inputs != 65535 && cfg.inputs != 8) ||
      (cfg.outputs != 65535 && cfg.outputs != 8)) {
    error = "USB requires a supported 8x8 integer PCM profile";
    return false;
  }
  const auto devices = pi5ausb::WindowsDevice::discover(error);
  const pi5ausb::UsbDevice *selected = nullptr;
  for (const auto &d : devices) {
    const std::string identity(d.hello.id.data(), d.hello.id.size());
    if (!expected_id.empty() && identity != expected_id)
      continue;
    if (selected) {
      error = "Select one USB DeviceId";
      return false;
    }
    selected = &d;
  }
  if (!selected) {
    error = "Requested AUSB device is unavailable";
    return false;
  }
  return open(cfg, expected_id, selected->path, error);
}
bool UsbSession::open(const UsbProfile &cfg, const std::string &expected_id,
                      const std::wstring &path, std::string &error) {
  profile_ = {cfg.rate, std::uint8_t(cfg.bits), 8, 8};
  if (!pi5ausb::valid_profile(profile_) || cfg.bits > 32 || path.empty() ||
      (cfg.inputs != 65535 && cfg.inputs != 8) ||
      (cfg.outputs != 65535 && cfg.outputs != 8)) {
    error = "Invalid USB path/profile";
    return false;
  }
  if (!usb_.open(path, error))
    return false;
  id_.assign(usb_.device().hello.id.data(), 32);
  if (!expected_id.empty() && id_ != expected_id) {
    error = "USB DeviceId changed at the selected path";
    return false;
  }
  return true;
}
bool UsbSession::run(ProcessBlock process, void *context, HANDLE stop,
                     unsigned seconds, unsigned depth, std::string &error,
                     const std::atomic<bool> *requested, unsigned jack_block) {
  if (!process || !depth || depth > 16 || seconds > 300) {
    error = "Invalid USB run/depth";
    return false;
  }
  const auto epoch = std::uint32_t(now_ns() | 1u);
  if (!usb_.configure(profile_, epoch, error,
                      std::clamp(jack_block, 32u, 256u)))
    return false;
  std::atomic<std::uint64_t> mmcss_failures{0};
  RealtimeThread realtime(mmcss_failures);
  SetThreadDescription(GetCurrentThread(), L"Rey Audio USB service audio");
  std::array<std::int32_t, 25 * 8> render{};
  std::array<std::uint8_t, pi5ausb::interrupt_packet_bytes> packet{};
  std::array<std::int32_t, 64 * 8> fifo{};
  std::uint64_t
      head = 1,
      tail = 0; // one silent frame covers rational cadence phase differences.
  pi5ausb::Cadence cadence(profile_.rate);
  const bool device_clock =
      usb_.device().hello.backend == pi5ausb::Backend::jack_clock;
  const unsigned batch = device_clock ? 4u : 1u;
  if (depth % batch) {
    error = "USB depth must contain complete render batches";
    return false;
  }
  std::array<std::uint8_t, pi5ausb::interrupt_packet_bytes * 4> grouped{};
  unsigned captured_frames = 0;
  std::uint32_t sequence = 0;
  std::uint64_t next_frame = 0, expected_frame = 0, callbacks = 0,
                underruns = 0, overruns = 0, gap_max = 0, previous = 0;
  auto submit = [&](unsigned slot, bool initial) {
    const auto frames =
        device_clock && !initial ? captured_frames : cadence.next();
    const pi5ausb::PacketInfo info{
        pi5ausb::Direction::render, 0, epoch, sequence++, next_frame,
        std::uint16_t(frames)};
    next_frame += frames;
    render.fill(0);
    if (!initial)
      for (unsigned f = 0; f < frames; ++f) {
        if (tail == head) {
          ++underruns;
          continue;
        }
        std::copy_n(fifo.data() + (tail++ % 64) * 8, 8, render.data() + f * 8);
      }
    std::size_t bytes = 0;
    if (!pi5ausb::write_header(packet.data(), packet.size(), profile_, info,
                               bytes) ||
        !pi5ausb::pack_pcm(packet.data() + 32, bytes - 32, render.data(),
                           frames * 8, profile_.bits)) {
      error = "USB packet encoding failed";
      return false;
    }
    if (initial && !usb_.submit_capture(slot, error))
      return false;
    if (batch > 1) {
      auto *destination =
          grouped.data() + (slot % batch) * pi5ausb::interrupt_packet_bytes;
      std::fill_n(destination, pi5ausb::interrupt_packet_bytes, 0);
      std::copy_n(packet.data(), bytes, destination);
      if ((slot + 1) % batch)
        return true;
      return usb_.submit_render_batch(slot / batch, grouped.data(),
                                      batch * pi5ausb::interrupt_packet_bytes,
                                      error);
    }
    return usb_.submit_render(slot, packet.data(), bytes, error);
  };
  for (unsigned slot = 0; slot < depth; ++slot)
    if (!submit(slot, true)) {
      usb_.stop();
      return false;
    }
  const auto start = now_ns(),
             end = seconds ? start + std::uint64_t(seconds) * 1000000000
                           : UINT64_MAX;
  struct Pending {
    std::array<std::int32_t, 25 * 8> capture{}, render{};
  };
  std::array<Pending, 16> pending{};
  std::array<AudioBlock, 16> blocks{};
  unsigned slot = 0;
  bool ok = true;
  while (WaitForSingleObject(stop, 0) != WAIT_OBJECT_0 && now_ns() < end &&
         (!requested || requested->load(std::memory_order_acquire))) {
    unsigned collected = 0;
    // Gather only completed IN transfers. Rearm each slot immediately; a
    // nonblocking probe never waits for another microframe to complete.
    while (collected < (device_clock ? depth : 1u)) {
      const auto current = (slot + collected) % depth;
      const std::uint8_t *data = nullptr;
      std::size_t bytes = 0;
      bool ready = false;
      if (!usb_.reap_capture(current, collected ? 0 : 100, data, bytes, ready,
                             error)) {
        ok = false;
        break;
      }
      if (!ready)
        break;
      const auto received = now_ns();
      if (previous)
        gap_max = std::max(gap_max, received - previous);
      previous = received;
      pi5ausb::PacketView view;
      const auto parsed = pi5ausb::parse_packet(
          data, bytes, profile_, pi5ausb::Direction::capture, epoch, view);
      auto &item = pending[collected];
      if (parsed != pi5ausb::PacketError::none ||
          view.info.first_frame != expected_frame ||
          view.info.sequence != std::uint32_t(callbacks) ||
          !pi5ausb::unpack_pcm(item.capture.data(), view.info.frames * 8,
                               view.pcm, view.pcm_bytes, profile_.bits)) {
        error = "USB capture timeline/PCM invalid";
        ok = false;
        break;
      }
      item.render.fill(0);
      blocks[collected] = {item.capture.data(),
                           item.render.data(),
                           view.info.frames,
                           8,
                           8,
                           profile_.bits,
                           0,
                           std::uint16_t(epoch),
                           view.info.first_frame,
                           received,
                           false};
      if (device_clock)
        blocks[collected].render_wait_ns = 125000;
      expected_frame += view.info.frames;
      ++callbacks;
      if (!usb_.submit_capture(current, error)) {
        ok = false;
        break;
      }
      ++collected;
    }
    if (!ok)
      break;
    if (!collected)
      continue;
    if (device_clock &&
        !publish_capture_batch(process, context, blocks.data(), collected)) {
      error = "USB capture callback stopped";
      ok = false;
      break;
    }
    for (unsigned i = 0; i < collected; ++i) {
      auto &block = blocks[i];
      if (!(device_clock ? render_captured_block(process, context, block)
                         : process(context, block))) {
        error = "USB render callback stopped";
        ok = false;
        break;
      }
      captured_frames = block.frames;
      for (unsigned f = 0; f < block.frames; ++f) {
        if (head - tail == 64) {
          ++overruns;
          error = "USB PCM layout queue overflow";
          ok = false;
          break;
        }
        std::copy_n(block.render + f * 8, 8, fifo.data() + (head++ % 64) * 8);
      }
      bool ready = false;
      if (!ok ||
          (((slot + 1) % batch == 0) &&
           (!usb_.reap_render(slot / batch, 100, ready, error) || !ready)) ||
          !submit(slot, false)) {
        ok = false;
        break;
      }
      slot = (slot + 1) % depth;
    }
    if (!ok)
      break;
  }
  usb_.stop();
  if (mmcss_failures)
    std::fprintf(stderr, "USB_MMCSS_FAILURES %llu\n",
                 static_cast<unsigned long long>(mmcss_failures.load()));
  std::printf(
      "USB_SERVICE_STATS seconds=%.3f callbacks=%llu frames=%llu "
      "layout_underruns=%llu "
      "layout_overruns=%llu rx_gap_max_us=%.3f usb_errors=%u\n",
      (now_ns() - start) / 1e9, static_cast<unsigned long long>(callbacks),
      static_cast<unsigned long long>(expected_frame),
      static_cast<unsigned long long>(underruns),
      static_cast<unsigned long long>(overruns), gap_max / 1000.0, ok ? 0 : 1);
  return ok && !underruns && !overruns;
}
} // namespace rey::engine
