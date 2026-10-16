#pragma once
#include "manager.hpp"
namespace rey::service {
#ifdef REY_FINITE_QUALIFICATION
inline constexpr wchar_t pipe_name[] = L"\\\\.\\pipe\\ReyAudio.Test.Qualification.v1";
#else
inline constexpr wchar_t pipe_name[] = L"\\\\.\\pipe\\ReyAudio.Control.v1";
#endif
// Control plane only. No PCM or audio callback waits enter this pipe.
bool serve(Manager &, HANDLE stop, std::string &error);
} // namespace rey::service
