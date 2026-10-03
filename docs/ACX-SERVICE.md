**English** | [Русский](ACX-SERVICE.ru.md)

# Single-Pi service and ACX development driver

This development branch contains a Windows network service without an ASIO SDK dependency and an initial ACX/KMDF kernel driver. It supports one Pi, one multichannel capture endpoint and one multichannel render endpoint, with up to eight channels in each direction. Selecting fewer channels changes the endpoint's channel count. Independent mono/stereo endpoints and multiple Pi devices remain separate future work.

The service has passed real UDP session tests and finite synthetic tests over the built-in Ethernet ports. The ACX driver has been compiled for x64; its INF has passed InfVerif and Inf2Cat. **It has not been signed, installed, loaded or exercised by WASAPI.** Those checks do not establish kernel correctness or audio endpoint functionality. The current Pi runtime generates synthetic PCM and has no connected ADC/DAC backend.

## Components and ownership

| Component | Responsibility |
|---|---|
| `AoIP-lib` | Packet protocol, CRC, frame-index Timeline, PCM codecs, channel maps and wire-budget accounting |
| `src/engine/PeerSession` | Stable identity, discovery, V3 subscription, lease, keepalive and unsubscribe |
| `src/engine/SessionEngine` | Single-session IOCP receive, timed PCM callback, bounded RX work and TX queue |
| `src/platform/StartGate` | One-shot worker readiness and epoch publication before remote PCM starts |
| `src/transport/IocpReceiver` | Overlapped receive pool, immutable consumer slots, cancellation and completion draining |
| `src/bridge/DriverBridge` | Versioned, bounded PCM exchange with the kernel driver |
| `apps/aoip_service.cpp` | Console/SCM lifetime, reconnect and optional digital echo fixture |
| `drivers/Acx` | Root control device, dynamic child, capture/render circuits and WaveRT state |
| `apps/wasapi_probe.cpp` | Endpoint inventory, IAudioClient3 period query and optional finite shared stream |

The existing ASIO adapter remains available. Its transport/codec components are shared, but its audio scheduling has not yet been fully replaced by SessionEngine. Keeping both implementations temporarily is a transition limit, not a claim of a completed common adapter architecture.

SessionEngine prepares the audio/RX/TX workers, their MMCSS state and timers before subscribing to the peer. StartGate releases them only after the acknowledged epoch has been published. Stop wakes workers whose session was never released. The gate adds no wait or allocation to the steady-state PCM callback. The updated service passed 13/13 Windows component tests, including delayed readiness, epoch publication, startup cancellation and real UDP session reopen.

The Pi identity prefers the board serial number. `PIAOIP_DEVICE_ID` can explicitly supply 32 nonzero lowercase hexadecimal characters; a malformed override is rejected. The machine-id fallback requires unique provisioning when images are cloned. A V3 peer rejects a second owner with BUSY while the first lease is active. Device identity and leases manage ownership; they are not network authentication.

The kernel control interface is exclusive and restricted to SYSTEM/administrators. The service owns its child device. Closing the service handle requests endpoint removal. Ordinary audio clients access the audio endpoints. The development bridge uses a fixed METHOD_BUFFERED exchange, not shared-memory zero-copy. It adds copies and a syscall per PCM block; its scheduling overhead still requires measurement.

## Build without ASIO

Use Windows x64, CMake 3.20+, Ninja and a complete LLVM-MinGW toolchain, or MSVC with the Windows SDK. Set `AOIP_SOURCE_DIR` to the matching AoIP-lib checkout when using a combined workspace.

```powershell
cmake -S . -B build/service -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DPIAOIP_BUILD_ASIO=OFF -DPIAOIP_BUILD_SERVICE=ON `
  -DAOIP_SOURCE_DIR=C:/work/AoIP-lib
cmake --build build/service --parallel 2
```

Outputs are `PiAoipService.exe` and `PiAoipWasapiProbe.exe`. There is no Steinberg header, ASIO registration or ASIO DLL requirement in this build. Minimal MinGW distributions may omit audio headers/import libraries; `PIAOIP_WASAPI_HEADERS` and `PIAOIP_SETUPAPI_LIBRARY` allow explicit paths to standard toolchain files. These are build inputs and are not bundled with the development package.

For the supported kernel build, use Visual Studio 2022 with the x64 MSVC toolset and the matching 26100 Windows SDK/WDK. The checked-in project specifies KMDF 1.31 and ACX 1.1:

```powershell
msbuild drivers/Acx/PiAoipAcx.vcxproj /p:Configuration=Release /p:Platform=x64
```

This MSVC project has not yet been executed on the current workstation, which lacks the installed MSVC/WDK integration. `tools/build_acx.py` supplies an isolated Clang 22 MSVC-ABI development build using official SDK/WDK headers and libraries. It performs InfVerif/Inf2Cat and records source/binary hashes; it never signs or installs. Custom compiler assembly/ABI checks and an actual supported-toolchain build remain qualification requirements. Do not distribute Microsoft SDK/WDK libraries or sample sources as project source.

## Run the finite transport fixture

The updated Pi peer must be running on the dedicated LAN and answer `PIAOIP_IDENTITY_V1`. For the tested PCM24 profile, configure the **Pi** to 192000 Hz, eight inputs/outputs and capture packet 32 frames before starting Windows. The service inherits the peer's rate and bit depth; INI `Rate`/`Bits` are not remote profile commands.

```powershell
PiAoipService.exe --console --echo --seconds 300 --peer 192.168.1.2 `
  --block 256 --guard 1536 --frames 32 --callback-us 50 --no-energy
```

Echo is a digital transport fixture. It does not create Windows endpoints or measure physical converters. The final `SERVICE_STATS` and `SERVICE_IO` lines include missing/late frames, deadline skips, queue/socket errors, callback duration, receive gaps and per-thread CPU. Preserving frame positions after a missed deadline matters more than replaying old PCM. The service does not silently change manual buffers.

`config/service.example.ini` now provides manual block256/guard1536, checked on fresh binaries for 300 seconds. A fresh guard1024 window had late/missing frames. Existing installed INI files are not rewritten. Subsequent block64/guard512 tests failed: one 900-second run had a host overrun; a 300-second run had 1440 late/missing frames and a 7.522 ms receive gap. That guard is not a confirmed stable baseline. Larger guards add delay and must be selected explicitly; the service does not retune them automatically. New LAN fixtures are capped at **300 seconds**. Use an absolute INI path for SCM. An optional `[Service] DeviceId` pins the expected physical board. Without it, identity is pinned for the process lifetime and a different board terminates that service run.

The updated service also completed 900 seconds with block64/guard1536/callback50. Missing/late/deadline/queue errors were zero, but host_overruns=1, so full qualification remains FAIL. Digital RTT p50/p95/p99/max was 8.392/8.532/8.567/13.446 ms. A larger guard does not change the 333.3 us callback period at block64/192 kHz. Crash/lease recovery passed; administrative link restoration and new ownership succeeded, but the separate guard512 PCM recovery failed with 3488 late/missing frames and 9 expired TX packets. The original Pi service, configuration, LAN/Wi-Fi and stopped test units were verified after the suite. These results do not qualify the SCM reconnect path or a real DAW.

The subsequent finite profile search used 15 short screens and three separate 900-second windows on the same binaries. Block128/guard448 failed with 5312 late/missing frames and one host overrun. Block256/guard1536 and block256/guard1024 both passed with zero PCM, deadline, overrun, queue/socket/IO, Pi loss/error and eth0 error/drop deltas. The fastest profile confirmed for 15 minutes in that search was **block256/guard1024/callback50**, eight inputs/outputs, 192 kHz, PCM24 and capture32. Digital RTT p50/p95/p99/max: **6.251/6.755/6.891/11.718 ms**; 3,373,249 samples. Windows audio/RX/TX CPU was 7.212/3.017/1.929% of one logical processor; Pi process CPU was 7.986% of one core. SERVICE_READY wire budget was 42.336 Mbps capture and 40.284 Mbps render. Lower guards512/768 at block256 passed only 30-second screens and are not confirmed stable. The original stand was restored and independently verified. This qualifies the specified steady digital window; recovery at guard1024, SCM, ACX/WASAPI, a real DAW and physical ADC/DAC remain untested. The example command above uses the larger profile checked in the release rebuild; the persisted INI is not changed automatically.

## Kernel stand and WASAPI checks

Prepare a separate Windows x64 test machine/VM with recovery access, the supported-toolchain build, driver debugging and a valid test-signing setup. The supplied SYS/CAT are unsigned and cannot establish an installable product. Signing/boot-security changes and a reboot on the live workstation require a separate explicit decision. Creating a root device is also required; staging an INF alone does not instantiate this root driver. On an already prepared signing stand, a WDK DevCon root-device installation uses `devcon install PiAoipAcx.inf Root\PiAoipAcx`.

After a signed root driver is installed and the updated Pi runtime is active, an elevated finite ACX console session is:

```powershell
PiAoipService.exe --console --seconds 30 --config C:/test/service.ini
PiAoipWasapiProbe.exe
```

Use another console for the probe while the service is still running. By default it selects PiAoIP audio endpoints; `--all` inventories active endpoints without streaming. An explicit endpoint ID selects one direction for a finite stream:

```powershell
PiAoipWasapiProbe.exe --id "endpoint-id-from-inventory" --flow capture --seconds 10
PiAoipWasapiProbe.exe --id "endpoint-id-from-inventory" --flow render --raw --seconds 10 --period 128
```

Render streams write digital silence; capture streams discard data. The tool queries legal default/fundamental/min/max periods and validates the requested shared period before opening. Event-gap percentiles describe application wakeups, not ADC/DAC latency. This tool currently covers shared mode; exclusive mode, simultaneous duplex, payload integrity and long real-DAW tests need their own qualification. Windows can impose a different effective period despite the driver's advertised constraints. [IAudioClient3 period query](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclient3-getsharedmodeengineperiod), [shared stream initialization](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclient3-initializesharedaudiostream).

Before release, exercise Driver Verifier, concurrent stream open/close, service crash, lease loss, cable disconnect/reconnect, child removal, wrong profiles, packet-number wrap, sleep/resume and root uninstall. Verify reported positions/QPC, capture overruns/render underruns, raw/shared/exclusive negotiation and kernel cancellation. A bounded PCM loop does not guarantee a bounded synchronous DeviceIoControl wait. Measure the service/driver/Windows-engine cost separately from network RTT. Physical latency still requires an ADC/DAC backend and hardware loopback.


## Release rebuild result

Fresh binaries, 300-second block256/guard1536/callback50 window: **PASS**, digital RTT p50/p95/p99/max **8.920/9.446/9.573/16.599 ms**, 1,123,192 samples. Primary Windows errors: zero.

Windows service SHA-256: `9b80dc78adc192cba79acbdfd6bb4a6ef7ba341cfe5862197782829bd1b38f63`. Pi5 SHA-256: `252a146bad247a75332b6710a52b04bd0a7360c3bae1fd47163d3e380a2aad1c`.

Callback/wake/RX gap/TX age max: 161.6/692.9/7270.0/884.0 us. Pi process CPU: 7.935% of one core. Full Windows worker CPU and all error/interface/throttle counters are in the linked JSON. Before/after throttle snapshots do not monitor power continuously.

The original installed Pi binary/configuration, LAN/Wi-Fi and CPU isolation were restored and independently checked. The runtime DEB peer hash matches this native candidate.

The earlier fresh-build block256/guard1024 case ran for 900 s and **FAILED**: late/missing=1568/1568, RX gap max=7427.9 us. Its low median and a prior clean window do not establish a reliable minimum. The new larger-guard case is a separate manual profile, with the unchanged wire budget. All subsequent cases are limited to 300 seconds by the project owner.
