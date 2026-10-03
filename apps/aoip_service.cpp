#include "../src/engine/session_engine.hpp"
#include "../src/bridge/driver_bridge.hpp"
#include <cwchar>
namespace piaoip {
HMODULE g_module = nullptr;
std::atomic<long> g_objects{0};
} // namespace piaoip
namespace {
HANDLE stop_event = nullptr;
SERVICE_STATUS_HANDLE status_handle = nullptr;
SERVICE_STATUS service_status{};
struct Options {
  piaoip::Config config;
  bool console = false, echo = false, scm = false;
  unsigned seconds = 0, callback_us = 0;
  std::string expected_id;
} options;
void report_status(DWORD state, DWORD error = NO_ERROR, DWORD service_error = 0) {
  service_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
  service_status.dwCurrentState = state;
  service_status.dwControlsAccepted =
      state == SERVICE_RUNNING ? SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN : 0;
  service_status.dwWin32ExitCode = error;
  service_status.dwServiceSpecificExitCode = service_error;
  service_status.dwWaitHint = state == SERVICE_STOP_PENDING ? 5000 : 0;
  if (status_handle)
    SetServiceStatus(status_handle, &service_status);
}
BOOL WINAPI console_control(DWORD code) {
  if (code == CTRL_C_EVENT || code == CTRL_BREAK_EVENT || code == CTRL_CLOSE_EVENT) {
    SetEvent(stop_event);
    return TRUE;
  }
  return FALSE;
}
DWORD WINAPI service_control(DWORD code, DWORD, LPVOID, LPVOID) {
  if (code == SERVICE_CONTROL_STOP || code == SERVICE_CONTROL_SHUTDOWN) {
    report_status(SERVICE_STOP_PENDING);
    SetEvent(stop_event);
    return NO_ERROR;
  }
  return code == SERVICE_CONTROL_INTERROGATE ? NO_ERROR : ERROR_CALL_NOT_IMPLEMENTED;
}
bool echo(void *context, const piaoip::engine::AudioBlock &block) {
  const auto delay = *static_cast<const unsigned *>(context);
  for (unsigned f = 0; f < block.frames; ++f)
    for (unsigned ch = 0; ch < block.outputs; ++ch)
      block.render[size_t(f) * block.outputs + ch] =
          ch < block.inputs ? block.capture[size_t(f) * block.inputs + ch] : 0;
  if (delay) {
    const auto until = piaoip::now_ns() + uint64_t(delay) * 1000;
    while (piaoip::now_ns() < until)
      YieldProcessor();
  }
  return true;
}
void print_stats(const piaoip::engine::SessionEngine &engine, double seconds) {
  const auto stats = engine.stats();
  const auto &d = stats.audio;
  const auto &io = stats.receive;
  std::printf("SERVICE_STATS seconds=%.3f rx=%llu invalid=%llu rx_overflow=%llu late_frames=%llu "
              "missing_frames=%llu resync=%llu callbacks=%llu deadline_misses=%llu "
              "skipped_frames=%llu callback_max_us=%.1f wake_max_us=%.1f rx_gap_max_us=%.1f "
              "tx=%llu tx_overflow=%llu tx_expired=%llu tx_errors=%llu mmcss_failures=%llu "
              "host_overruns=%llu expired_output_frames=%llu control_failures=%llu "
              "budget_yields=%llu suppressed_frames=%llu tx_age_max_us=%.1f\n",
              seconds, d.rx_packets, d.invalid_packets, d.rx_queue_overflows, d.late_frames,
              d.missing_frames, d.resyncs, d.callbacks, d.deadline_misses, d.skipped_frames,
              d.max_callback_ns / 1000.0, d.max_wake_late_ns / 1000.0, d.max_rx_gap_ns / 1000.0,
              d.tx_packets, d.tx_queue_overflows, d.tx_expired_packets, d.tx_errors,
              d.mmcss_failures, d.host_overruns, d.expired_output_frames, stats.control_failures,
              stats.budget_yields, stats.suppressed_frames, stats.max_tx_age_ns / 1000.0);
  std::printf("SERVICE_IO calls=%llu completions=%llu waits=%llu max_batch=%llu overflows=%llu "
              "errors=%llu audio_percent=%.3f rx_percent=%.3f tx_percent=%.3f\n",
              io.calls, io.completions, io.waits, io.max_batch, io.overflows, io.errors,
              stats.audio_cpu_ns / (seconds * 1e7), stats.rx_cpu_ns / (seconds * 1e7),
              stats.tx_cpu_ns / (seconds * 1e7));
  std::fflush(stdout);
}
int run() {
  const auto end =
      options.seconds ? piaoip::now_ns() + uint64_t(options.seconds) * 1000000000 : UINT64_MAX;
  std::string identity = options.expected_id;
  while (WaitForSingleObject(stop_event, 0) != WAIT_OBJECT_0 && piaoip::now_ns() < end) {
    piaoip::engine::SessionEngine engine;
    piaoip::bridge::DriverBridge bridge;
    std::string error;
    if (!engine.open(options.config, error)) {
      std::fprintf(stderr, "SERVICE_CONNECT %s\n", error.c_str());
      if (options.console)
        return 2;
      WaitForSingleObject(stop_event, 1000);
      continue;
    }
    if (!identity.empty() && identity != engine.peer().identity()) {
      std::fprintf(stderr, "SERVICE_IDENTITY_MISMATCH\n");
      return 3;
    }
    identity = engine.peer().identity();
    if (!options.echo && !bridge.attach(engine, error)) {
      std::fprintf(stderr, "SERVICE_BRIDGE %s\n", error.c_str());
      if (options.console)
        return 4;
      WaitForSingleObject(stop_event, 1000);
      continue;
    }
    auto process =
        options.echo ? echo : +[](void *context, const piaoip::engine::AudioBlock &block) {
          return static_cast<piaoip::bridge::DriverBridge *>(context)->process(block);
        };
    void *context =
        options.echo ? static_cast<void *>(&options.callback_us) : static_cast<void *>(&bridge);
    if (!engine.start(process, context, error)) {
      std::fprintf(stderr, "SERVICE_START %s\n", error.c_str());
      if (options.console)
        return 5;
      WaitForSingleObject(stop_event, 1000);
      continue;
    }
    const auto &cfg = engine.config();
    const auto &budget = engine.peer().budget();
    std::printf("SERVICE_READY id=%s backend=%s mode=%s rate=%u bits=%u inputs=%u outputs=%u "
                "block=%ld guard=%u capture_frames=%u capture_pps=%llu render_pps=%llu "
                "capture_bps=%llu render_bps=%llu\n",
                engine.peer().identity(), engine.peer().backend(),
                options.echo ? "digital-echo" : "acx", cfg.rate, cfg.bits,
                engine.peer().inputs().count, engine.peer().outputs().count, cfg.block, cfg.safety,
                engine.peer().capture_frames(), budget.capture.packets_per_second_ceiling,
                budget.render.packets_per_second_ceiling, budget.capture.wire_bits_per_second,
                budget.render.wire_bits_per_second);
    std::fflush(stdout);
    const auto start = piaoip::now_ns();
    while (engine.running()) {
      const auto now = piaoip::now_ns();
      if (now >= end)
        break;
      const DWORD timeout =
          end == UINT64_MAX
              ? INFINITE
              : DWORD(std::min<uint64_t>(INFINITE - 1, (end - now + 999999) / 1000000));
      if (engine.wait_until_stopped(stop_event, timeout))
        break;
    }
    const bool lost = !engine.running();
    engine.stop();
    const double elapsed = (piaoip::now_ns() - start) / 1e9;
    print_stats(engine, elapsed);
    bridge.detach();
    if (options.console)
      return lost ? 6 : 0;
    if (lost)
      WaitForSingleObject(stop_event, 1000);
  }
  return 0;
}
void WINAPI service_main(DWORD, LPWSTR *) {
  status_handle = RegisterServiceCtrlHandlerExW(L"PiAoipService", service_control, nullptr);
  if (!status_handle)
    return;
  report_status(SERVICE_START_PENDING);
  report_status(SERVICE_RUNNING);
  const int result = run();
  report_status(SERVICE_STOPPED, result ? ERROR_SERVICE_SPECIFIC_ERROR : NO_ERROR, DWORD(result));
}
bool number(const wchar_t *text, unsigned &value, unsigned maximum) {
  if (!text || *text < L'0' || *text > L'9')
    return false;
  wchar_t *end = nullptr;
  const auto parsed = std::wcstoul(text, &end, 10);
  if (*end || parsed > maximum)
    return false;
  value = unsigned(parsed);
  return true;
}
} // namespace
int wmain(int argc, wchar_t **argv) {
  piaoip::g_module = GetModuleHandleW(nullptr);
  for (int i = 1; i < argc; ++i) {
    const std::wstring arg = argv[i];
    if (arg == L"--console")
      options.console = true;
    else if (arg == L"--service")
      options.scm = true;
    else if (arg == L"--echo")
      options.echo = true;
    else if (arg == L"--no-energy")
      options.config.energy_saving = false;
    else if (i + 1 < argc && arg == L"--peer") {
      if (!WideCharToMultiByte(CP_UTF8, 0, argv[++i], -1, options.config.peer,
                               sizeof(options.config.peer), nullptr, nullptr))
        return 1;
    } else if (i + 1 < argc && arg == L"--config") {
      const auto *path = argv[++i];
      if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
        std::fprintf(stderr, "Configuration file is missing\n");
        return 1;
      }
      piaoip::read_config_file(path, options.config);
      wchar_t id[40]{};
      GetPrivateProfileStringW(L"Service", L"DeviceId", L"", id, 40, path);
      char text[40]{};
      WideCharToMultiByte(CP_UTF8, 0, id, -1, text, sizeof(text), nullptr, nullptr);
      options.expected_id = text;
    } else if (i + 1 < argc &&
               (arg == L"--seconds" || arg == L"--block" || arg == L"--guard" ||
                arg == L"--frames" || arg == L"--callback-us" || arg == L"--port")) {
      unsigned value = 0;
      const unsigned limit =
          arg == L"--seconds"
              ? 3600
              : arg == L"--guard" ? 8192
                                  : arg == L"--callback-us" ? 500 : arg == L"--port" ? 65535 : 256;
      if (!number(argv[++i], value, limit))
        return 1;
      if (arg == L"--seconds")
        options.seconds = value;
      else if (arg == L"--block")
        options.config.block = value;
      else if (arg == L"--guard")
        options.config.safety = value;
      else if (arg == L"--frames")
        options.config.capture_frames = value;
      else if (arg == L"--port")
        options.config.port = uint16_t(value);
      else
        options.callback_us = value;
    } else {
      std::fprintf(
          stderr,
          "Usage: PiAoipService --console [--echo] --seconds 1..3600 --peer IPv4 [--block "
          "16..256 --guard N --frames N --port 0..65535 --callback-us N --config absolute.ini "
          "--no-energy]\nSCM: PiAoipService --service --config absolute.ini\n");
      return 1;
    }
  }
  if (options.console == options.scm || (options.console && !options.seconds) ||
      (options.scm && (options.echo || options.seconds || options.callback_us)))
    return 1;
  stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!stop_event)
    return 1;
  int result = 0;
  if (options.scm) {
    SERVICE_TABLE_ENTRYW table[] = {{const_cast<wchar_t *>(L"PiAoipService"), service_main},
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
