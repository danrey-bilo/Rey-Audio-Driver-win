#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <windows.h>
#include <cstdint>
#include <cstddef>
#include <string>

namespace rey::asio {
constexpr uint32_t magic = 0x52415349, version = 1;
constexpr unsigned channels = 8, max_frames = 256, slots = 8;
constexpr long diagnostics_selector = 0x52455931;
inline constexpr GUID clsid = {0x91ba2c4a, 0x56bc, 0x4c49,
    {0xa4, 0x99, 0x23, 0x19, 0x79, 0xec, 0x5f, 0x60}};

// Cross-process ABI uses Windows interlocked words, never C++ pointers or locks.
struct alignas(64) Counter { volatile LONG64 value; uint8_t reserved[56]; };
inline uint64_t load(Counter &v) { return uint64_t(InterlockedCompareExchange64(&v.value, 0, 0)); }
inline void store(Counter &v, uint64_t n) { InterlockedExchange64(&v.value, LONG64(n)); }
inline void add(Counter &v, uint64_t n = 1) { InterlockedExchangeAdd64(&v.value, LONG64(n)); }
struct Slot {
  uint64_t frame_position, timestamp_ns;
  uint32_t frames, flags;
  int32_t pcm[max_frames * channels]; // interleaved, right-aligned signed valid bits
};
struct Ring { Counter written, read; Slot data[slots]; };
struct Shared {
  uint32_t magic_value, abi_version, bytes, block, lead_blocks, rate, bits, depth;
  char device_id[32];
  Counter ready, running, capture_dropped, render_late, render_missing;
  Counter render_overflow, capture_gap_max_ns;
  Counter clock_position;
  Counter client_capture_dropped;
  Ring capture, render;
};
static_assert(alignof(Counter) == 64 && sizeof(Counter) == 64);
static_assert(offsetof(Shared, capture) % 64 == 0);
static_assert(sizeof(Shared) < 1024 * 1024);
inline bool valid_block(unsigned n) { return n >= 16 && n <= max_frames && !(n & (n - 1)); }
inline bool valid_rate(unsigned n) {
  return n == 44100 || n == 48000 || n == 88200 || n == 96000 || n == 176400 || n == 192000;
}
struct Info {
  std::string id;
  unsigned rate = 0, bits = 0, block = 64, depth = 3;
  uint64_t generation = 0;
};
struct Diagnostics {
  uint32_t size = sizeof(Diagnostics), abi_version = version, running = 0, block = 0;
  uint64_t callbacks = 0, capture_dropped = 0, render_late_frames = 0, render_missing_frames = 0;
  uint64_t render_overflow = 0, callback_gap_max_ns = 0, callback_processing_max_ns = 0;
  uint64_t capture_to_callback_max_ns = 0, service_gap_max_ns = 0, mmcss_failures = 0;
};
} // namespace rey::asio
