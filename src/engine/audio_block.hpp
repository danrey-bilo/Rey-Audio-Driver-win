#pragma once
#include <cstdint>
namespace rey::engine {
struct AudioBlock {
  const int32_t *capture = nullptr;
  int32_t *render = nullptr;
  unsigned frames = 0, inputs = 0, outputs = 0, bits = 0;
  unsigned missing = 0;
  uint16_t epoch = 0;
  uint64_t frame_position = 0, scheduled_ns = 0;
  bool discontinuity = false;
  // Capture batches need time for the ASIO host before their corresponding render.
  // WinUSB delivers microframes and leaves this at zero.
  unsigned render_delay_frames = 0;
  bool capture_only = false, render_only = false;
  // An already queued native USB transfer can briefly yield its unused
  // render deadline to the ASIO callback. Other transports leave this zero.
  unsigned render_wait_ns = 0;
};
using ProcessBlock = bool (*)(void *, const AudioBlock &);
} // namespace rey::engine
