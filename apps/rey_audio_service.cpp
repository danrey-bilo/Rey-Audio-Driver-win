#include "../src/service/pipe_server.hpp"
#include <cwchar>
namespace piaoip {
HMODULE g_module = nullptr;
std::atomic<long> g_objects{0};
} // namespace piaoip
namespace {
HANDLE stop_event = nullptr;
SERVICE_STATUS_HANDLE status_handle = nullptr;
SERVICE_STATUS status{};
std::wstring configuration;
bool digital_test = false;
unsigned seconds = 0;
void report(DWORD state, DWORD error = NO_ERROR) {
  status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
  status.dwCurrentState = state;
  status.dwControlsAccepted =
      state == SERVICE_RUNNING ? SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN
                               : 0;
  status.dwWin32ExitCode = error;
  status.dwWaitHint =
      state == SERVICE_START_PENDING || state == SERVICE_STOP_PENDING ? 10000
                                                                      : 0;
  if (status_handle)
    SetServiceStatus(status_handle, &status);
}
DWORD WINAPI control(DWORD code, DWORD, void *, void *) {
  if (code == SERVICE_CONTROL_STOP || code == SERVICE_CONTROL_SHUTDOWN) {
    report(SERVICE_STOP_PENDING);
    SetEvent(stop_event);
    return NO_ERROR;
  }
  return code == SERVICE_CONTROL_INTERROGATE ? NO_ERROR
                                             : ERROR_CALL_NOT_IMPLEMENTED;
}
BOOL WINAPI console_control(DWORD code) {
  if (code == CTRL_C_EVENT || code == CTRL_BREAK_EVENT ||
      code == CTRL_CLOSE_EVENT) {
    SetEvent(stop_event);
    return TRUE;
  }
  return FALSE;
}
int run() {
  rey::service::Manager manager(configuration, digital_test);
  std::string error;
  if (!manager.initialize(error)) {
    std::fprintf(stderr, "REY_SERVICE_INIT %s\n", error.c_str());
    return 2;
  }
  std::thread audio([&] { manager.run(stop_event); });
  std::thread timer;
  if (seconds)
    timer = std::thread([] {
      if (WaitForSingleObject(stop_event, seconds * 1000) == WAIT_TIMEOUT)
        SetEvent(stop_event);
    });
  report(SERVICE_RUNNING);
  std::printf("REY_SERVICE_READY digital_test=%u\n", unsigned(digital_test));
  std::fflush(stdout);
  const bool ok = rey::service::serve(manager, stop_event, error);
  SetEvent(stop_event);
  audio.join();
  if (timer.joinable())
    timer.join();
  if (!ok)
    std::fprintf(stderr, "REY_CONTROL_PIPE %s\n", error.c_str());
  return ok ? 0 : 3;
}
void WINAPI service_main(DWORD, LPWSTR *) {
  status_handle =
      RegisterServiceCtrlHandlerExW(L"ReyAudioService", control, nullptr);
  if (!status_handle)
    return;
  report(SERVICE_START_PENDING);
  report(SERVICE_STOPPED, run() ? ERROR_SERVICE_SPECIFIC_ERROR : NO_ERROR);
}
} // namespace
int wmain(int argc, wchar_t **argv) {
  bool console = false, scm = false;
  piaoip::g_module = GetModuleHandleW(nullptr);
  for (int i = 1; i < argc; ++i) {
    const std::wstring arg = argv[i];
    if (arg == L"--console")
      console = true;
    else if (arg == L"--service")
      scm = true;
    else if (arg == L"--digital-test")
      digital_test = true;
    else if (arg == L"--config" && i + 1 < argc)
      configuration = argv[++i];
    else if (arg == L"--seconds" && i + 1 < argc) {
      const std::wstring text = argv[++i];
      std::string ascii(text.begin(), text.end());
      if (!rey::service::parse_unsigned(ascii, seconds, 300) || !seconds)
        return 1;
    } else {
      std::fprintf(stderr,
                   "Usage: ReyAudioService --service --config absolute.ini\n"
                   "Test: ReyAudioService --console --seconds 1..300 "
                   "[--digital-test] --config absolute.ini\n");
      return 1;
    }
  }
  if (console == scm || (console && !seconds) ||
      (scm && (seconds || digital_test)) || configuration.size() < 4 ||
      configuration[1] != L':' ||
      (configuration[2] != L'\\' && configuration[2] != L'/'))
    return 1;
  stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!stop_event)
    return 1;
  int result = 0;
  if (scm) {
    SERVICE_TABLE_ENTRYW table[] = {
        {const_cast<wchar_t *>(L"ReyAudioService"), service_main},
        {nullptr, nullptr}};
    if (!StartServiceCtrlDispatcherW(table))
      result = int(GetLastError());
  } else {
    SetConsoleCtrlHandler(console_control, TRUE);
    result = run();
    SetConsoleCtrlHandler(console_control, FALSE);
  }
  CloseHandle(stop_event);
  return result;
}
