#pragma once
#include "manager.hpp"
namespace rey::service {
inline constexpr wchar_t pipe_name[] = L"\\\\.\\pipe\\ReyAudio.Control.v1";
// Control plane only. No PCM or audio callback waits enter this pipe.
bool serve(Manager &, HANDLE stop, std::string &error);
} // namespace rey::service
