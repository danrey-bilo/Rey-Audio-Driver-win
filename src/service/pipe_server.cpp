#include "pipe_server.hpp"
#if __has_include(<sddl.h>)
#include <sddl.h>
#else
extern "C" __declspec(dllimport) BOOL WINAPI
ConvertStringSecurityDescriptorToSecurityDescriptorW(LPCWSTR, DWORD,
                                                     PSECURITY_DESCRIPTOR *,
                                                     PULONG);
#define SDDL_REVISION_1 1u
#endif
namespace rey::service {
namespace {
bool complete(HANDLE pipe, OVERLAPPED &operation, HANDLE stop, DWORD timeout,
              DWORD &bytes) {
  const HANDLE waits[]{stop, operation.hEvent};
  const auto result = WaitForMultipleObjects(2, waits, FALSE, timeout);
  if (result != WAIT_OBJECT_0 + 1) {
    CancelIoEx(pipe, &operation);
    GetOverlappedResult(pipe, &operation, &bytes,
                        TRUE); // drain before reusing stack storage
    return false;
  }
  return GetOverlappedResult(pipe, &operation, &bytes, FALSE) != FALSE;
}
bool exchange(HANDLE pipe, OVERLAPPED &operation, HANDLE stop, void *data,
              DWORD length, DWORD &bytes, bool write) {
  const auto event = operation.hEvent;
  operation = {};
  operation.hEvent = event;
  ResetEvent(event);
  const bool ok =
      (write ? WriteFile(pipe, data, length, &bytes, &operation)
             : ReadFile(pipe, data, length, &bytes, &operation)) != FALSE;
  if (ok)
    return true;
  if (GetLastError() != ERROR_IO_PENDING)
    return false;
  return complete(pipe, operation, stop, 2000, bytes);
}
} // namespace
bool serve(Manager &manager, HANDLE stop, std::string &error) {
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
          L"D:P(D;;GA;;;NU)(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGW;;;IU)",
          SDDL_REVISION_1, &descriptor, nullptr)) {
    error = "Cannot create control pipe security";
    return false;
  }
  SECURITY_ATTRIBUTES security{sizeof(security), descriptor, FALSE};
  const auto pipe = CreateNamedPipeW(pipe_name,
                                     PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED |
                                         FILE_FLAG_FIRST_PIPE_INSTANCE,
                                     PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE |
                                         PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                                     1, 32768, 1024, 0, &security);
  LocalFree(descriptor);
  if (pipe == INVALID_HANDLE_VALUE) {
    error = "Cannot create control pipe: " + std::to_string(GetLastError());
    return false;
  }
  OVERLAPPED operation{};
  operation.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!operation.hEvent) {
    CloseHandle(pipe);
    error = "Cannot allocate pipe event";
    return false;
  }
  while (WaitForSingleObject(stop, 0) != WAIT_OBJECT_0) {
    ResetEvent(operation.hEvent);
    DWORD bytes = 0;
    bool connected = ConnectNamedPipe(pipe, &operation) != FALSE;
    if (!connected) {
      const auto code = GetLastError();
      connected = code == ERROR_PIPE_CONNECTED ||
                  (code == ERROR_IO_PENDING &&
                   complete(pipe, operation, stop, INFINITE, bytes));
    }
    if (connected) {
      std::array<char, 1024> command{};
      if (exchange(pipe, operation, stop, command.data(), DWORD(command.size()),
                   bytes, false) &&
          bytes > 0 && bytes < command.size()) {
        std::string request(command.data(), bytes);
        bool clean = true;
        for (const auto c : request)
          clean = clean && c >= 32 && c <= 126;
        auto reply =
            clean
                ? manager.request(request)
                : std::string(
                      "{\"ok\":false,\"error\":\"Invalid command encoding\"}");
        if (reply.size() < 32768 &&
            exchange(pipe, operation, stop, reply.data(), DWORD(reply.size()),
                     bytes, true)) {
          // A completed pipe write means buffered, not consumed by the client.
          // Disconnect immediately would discard a WPF client's unread reply.
          // Wait for its ACK or close, with the same bounded cancellation path.
          std::array<char, 8> ack{};
          exchange(pipe, operation, stop, ack.data(), DWORD(ack.size()), bytes,
                   false);
        }
      }
    }
    DisconnectNamedPipe(pipe);
    const auto event = operation.hEvent;
    operation = {};
    operation.hEvent = event;
  }
  CloseHandle(operation.hEvent);
  CloseHandle(pipe);
  return true;
}
} // namespace rey::service
