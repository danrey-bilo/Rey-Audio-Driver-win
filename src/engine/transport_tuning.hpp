#pragma once
#include <array>
#include <cstdint>
namespace rey::engine {
struct TransportTuning {
  unsigned rate, requests, reserve_us;
};
// Internal transport policy. The host remains free to select its ASIO buffer.
inline constexpr std::array<TransportTuning, 6> transport_tunings{
    {{44100, 8, 1000},
     {48000, 8, 1000},
     {88200, 8, 750},
     {96000, 8, 750},
     {176400, 8, 1500},
     {192000, 8, 1500}}};
constexpr TransportTuning transport_tuning(unsigned rate) {
  for (const auto &value : transport_tunings)
    if (value.rate == rate)
      return value;
  return {rate, 8, 1500};
}
constexpr unsigned render_lead(unsigned rate, unsigned block,
                               bool uac2 = false) {
  if (uac2)
    return 1;
  const auto frames =
      (uint64_t(rate) * transport_tuning(rate).reserve_us + 999999) / 1000000;
  return unsigned((frames + block - 1) / block) + 1;
}
} // namespace rey::engine
