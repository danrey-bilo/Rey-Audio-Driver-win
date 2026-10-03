#pragma once
#include <windows.h>
#include <mmreg.h>
#include <cstddef>

// Native C interoperability layout for TAG 2.0, driver API v5.
// Keep the external SDK and original signed driver outside this source tree.
namespace rey::bridge::gateway {
#pragma pack(push, 4)
struct Range {
  UINT min_rate, max_rate, min_bits, max_bits, min_channels, max_channels;
  UINT min_container, max_container;
};
struct Format { UINT rate, bits, channels, container, mask; };
struct Info { UINT major, minor, revision, build; Range formats; UINT lines, flags; };
struct Line { UINT id; bool capture; UINT type; wchar_t ks_name[64], endpoint_name[32]; };
struct VolumeParams { bool enabled; int min_db, max_db; UINT step_db; };
struct VolumeState { bool mute[32]; int level[32]; };
struct Filter {
  UINT flags, notification;
  Range formats;
  Format default_format;
  UINT reserved_bytes, minimum_us;
  VolumeParams volume;
  bool has_stream;
  UINT overruns, underruns;
  VolumeState state;
};
struct Stream {
  UINT flags, process, thread;
  WAVEFORMATEXTENSIBLE format;
  UINT state, buffer_frames, buffered_frames;
  UINT64 transferred_frames;
  UINT client_position, overruns, underruns;
};
struct Common {
  struct { Filter filter; Stream stream; } driver;
  struct { void *buffer; UINT position, position_bytes; LONG status; bool ready; } host;
};
#pragma pack(pop)
static_assert(sizeof(Range) == 32 && sizeof(Info) == 56 && sizeof(Line) == 204);
static_assert(sizeof(Filter) == 256 && sizeof(Stream) == 84 && sizeof(Common) == 364);
static_assert(offsetof(Common, host) == 340);
inline constexpr GUID product = {
    0x4d699d4a, 0x65a5, 0x40ec, {0x98, 0x75, 0x8e, 0x6d, 0x5f, 0xc0, 0x1e, 0x0c}};
inline constexpr GUID pcm = {
    1, 0, 0x0010, {0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71}};
inline constexpr UINT waiting_for_host = 4;
inline constexpr UINT stream_running = 3;
inline constexpr LONG format_not_supported = LONG(0xc00000bbu);
} // namespace rey::bridge::gateway
