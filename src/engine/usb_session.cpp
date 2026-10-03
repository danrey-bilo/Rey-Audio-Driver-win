#include "usb_session.hpp"
#include "pi5ausb/pcm.hpp"
#include <array>
namespace piaoip::engine {
bool UsbSession::open(const Config &cfg, const std::string &expected_id,
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
  id_ = std::string(selected->hello.id.data(), 32);
  return usb_.open(selected->path, error);
}
bool UsbSession::run(ProcessBlock process, void *context, HANDLE stop,
                     unsigned seconds, unsigned depth, std::string &error) {
  if (!process || !depth || depth > 16 || seconds > 300) {
    error = "Invalid USB run/depth";
    return false;
  }
  const auto epoch = std::uint16_t((now_ns() & 65535u) | 1u);
  if (!usb_.configure(profile_, epoch, error))
    return false;
  std::atomic<std::uint64_t> mmcss_failures{0};
  RealtimeThread realtime(mmcss_failures);
  SetThreadDescription(GetCurrentThread(), L"Rey Audio USB service audio");
  std::array<std::int32_t, 25 * 8> capture{}, render{};
  std::array<std::uint8_t, pi5ausb::interrupt_packet_bytes> packet{};
  std::array<std::int32_t, 64 * 8> fifo{};
  std::uint64_t
      head = 1,
      tail = 0; // one silent frame covers rational cadence phase differences.
  pi5ausb::Cadence cadence(profile_.rate);
  std::uint32_t sequence = 0;
  std::uint64_t next_frame = 0, expected_frame = 0, callbacks = 0,
                underruns = 0, overruns = 0, gap_max = 0, previous = 0;
  auto submit = [&](unsigned slot, bool initial) {
    const auto frames = cadence.next();
    const pi5ausb::PacketInfo info{
        pi5ausb::Direction::render, 0, epoch, sequence++, next_frame, frames};
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
    return usb_.submit_capture(slot, error) &&
           usb_.submit_render(slot, packet.data(), bytes, error);
  };
  for (unsigned slot = 0; slot < depth; ++slot)
    if (!submit(slot, true)) {
      usb_.stop();
      return false;
    }
  const auto start = now_ns(),
             end = seconds ? start + std::uint64_t(seconds) * 1000000000
                           : UINT64_MAX;
  unsigned slot = 0;
  bool ok = true;
  while (WaitForSingleObject(stop, 0) != WAIT_OBJECT_0 && now_ns() < end) {
    const std::uint8_t *data = nullptr;
    std::size_t bytes = 0;
    bool ready = false;
    if (!usb_.reap_capture(slot, 100, data, bytes, ready, error)) {
      ok = false;
      break;
    }
    if (!ready)
      continue;
    const auto received = now_ns();
    if (previous)
      gap_max = std::max(gap_max, received - previous);
    previous = received;
    pi5ausb::PacketView view;
    if (pi5ausb::parse_packet(data, bytes, profile_,
                              pi5ausb::Direction::capture, epoch,
                              view) != pi5ausb::PacketError::none ||
        view.info.first_frame != expected_frame ||
        view.info.sequence != std::uint32_t(callbacks) ||
        !pi5ausb::unpack_pcm(capture.data(), view.info.frames * 8, view.pcm,
                             view.pcm_bytes, profile_.bits)) {
      error = "USB capture timeline/PCM invalid";
      ok = false;
      break;
    }
    render.fill(0);
    const AudioBlock block{capture.data(),
                           render.data(),
                           view.info.frames,
                           8,
                           8,
                           profile_.bits,
                           0,
                           epoch,
                           view.info.first_frame,
                           received,
                           false};
    if (!process(context, block)) {
      error = "USB audio callback stopped";
      ok = false;
      break;
    }
    expected_frame += view.info.frames;
    ++callbacks;
    for (unsigned f = 0; f < view.info.frames; ++f) {
      if (head - tail == 64) {
        ++overruns;
        error = "USB PCM layout queue overflow";
        ok = false;
        break;
      }
      std::copy_n(render.data() + f * 8, 8, fifo.data() + (head++ % 64) * 8);
    }
    if (!ok || !usb_.reap_render(slot, 100, ready, error) || !ready ||
        !submit(slot, false)) {
      ok = false;
      break;
    }
    slot = (slot + 1) % depth;
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
} // namespace piaoip::engine
