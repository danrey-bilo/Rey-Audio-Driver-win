#pragma once
#include "../platform/windows.hpp"
namespace rey::engine {
// The service's USB format. There are no network settings or channel masks.
struct UsbProfile {
  uint32_t rate = 192000;
  uint16_t bits = 32;
  unsigned block = 64, safety = 0;
  unsigned inputs = 8, outputs = 8;
};
}
