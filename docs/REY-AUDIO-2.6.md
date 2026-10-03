**English** | [Русский](REY-AUDIO-2.6.ru.md)

# Rey Audio Driver 2.6.0 preview

Rey Audio Driver separates the USB and AoIP connections into one background
Windows service and a new desktop/tray panel. The service and panel build without
the ASIO SDK. One Raspberry Pi 5 is supported in this preview.

## What works

| Component | Behavior |
|---|---|
| `ReyAudioService.exe` | SCM service, automatic startup, USB interface discovery, reconnect, one active transport session |
| `ReyAudioControl.exe` | Separate USB/LAN pages, manual settings, tray, real service status, bounded event history |
| USB | 8 inputs and 8 outputs; PCM16/24/32; 44.1, 48, 88.2, 96, 176.4 and 192 kHz; High-Speed interrupt/WinUSB |
| AoIP | Existing UDP/IPv4/IOCP engine, manual block/guard/packet size, optional silence suppression |
| ACX/KMDF | Own root bridge and dynamic input/output child; new Rey names; development SYS/INF/CAT build passes |

The ACX driver is **unsigned and has not been installed or exercised through
WASAPI**. Installing the user-mode service detects the Pi and exposes its
settings, but does not itself create Windows audio endpoints. The panel displays
this state explicitly. The Pi USB backend currently performs digital loopback;
device-clocked ADC/DAC, clock feedback and physical audio qualification remain
open. This package is a Pre-release.

## Connection and settings

USB interfaces are enumerated once per second without opening the exclusive
stream handle. HELLO validates the board's stable 32-digit DeviceId and the
High-Speed descriptor. An active stream remains visible to discovery. Software
disconnect/re-enumeration was tested by stopping and restarting only the Pi USB
gadget service. A physical unplug with power loss and Windows sleep/resume have
not been tested.

Open the tray panel, choose **AoIP / LAN**, enter the Pi's IPv4 address or use
network discovery, and connect. A configured LAN connection has priority by
default. **Use USB** selects USB explicitly. Switching stops and drains the old
session before opening the new one; switching is not seamless. Automatic LAN-to-
USB failover is not implemented: a requested LAN session retries until the user
disconnects it or selects USB. Several physical Pi devices are deferred.

`[USB]` and `[AoIP]` are persisted independently in
`%ProgramData%/ReyAudio/service.ini`. Editing an inactive transport's profile does
not restart the active transport. Invalid commands leave both profiles unchanged.
There is no automatic buffer tuning. The example LAN block256/guard1536/packet32
comes from the previous conservative digital validation; it is not a universal
latency guarantee. LAN rate/bits are obtained from the Pi and must be configured
on that Pi. USB rate/bits are negotiated by its own control protocol.

The initial panel language is Russian. Keyboard focus, native window resizing,
scrolling and named controls are provided. Closing the window moves it to the
tray; **Exit panel** closes only the panel. The service continues independently.
The main pages show connection settings; diagnostics contains backend/DeviceId,
callback counters and events. Callback gaps are not RTT or ADC/DAC latency.

## Install the user-mode components

Extract the development ZIP. From an Administrator PowerShell:

```powershell
./tools/install_rey.ps1 -Package C:/work/Rey-Audio-Driver-2.6.0-preview-x64
```

The installer verifies the three executable hashes, copies them to
`%ProgramFiles%/Rey Audio Driver`, preserves existing settings, creates the
automatic `ReyAudioService`, configures recovery, and starts it. The tray panel
runs at user sign-in. Only the service runs as SYSTEM to access the protected ACX
control interface. The panel remains an ordinary user process.

The LAN firewall rule is restricted to this service, UDP50021, the Private
profile and LocalSubnet. A manually changed local UDP port needs an appropriate
manual firewall rule. Public network rules are not created.

Removal stops and removes only the own service, tray autorun and firewall rule:

```powershell
./tools/install_rey.ps1 -Package C:/work/Rey-Audio-Driver-2.6.0-preview-x64 -Remove
```

Files and settings remain for rollback. The installer does not enable test
signing, change boot security, reboot, or install the kernel package. On an
already prepared driver stand, the root hardware ID is `Root\ReyAudioAcx`.
Signing, supported-toolchain validation, Driver Verifier, cancellation and real
WASAPI/DAW streaming are still required before a production release.

## Build and structure

```powershell
cmake -S . -B build/rey -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DPIAOIP_BUILD_ASIO=OFF -DPIAOIP_ENABLE_USB=ON `
  -DAOIP_SOURCE_DIR=C:/work/AoIP-lib -DPI5AUSB_SOURCE_DIR=C:/work/Pi5-AUSB
cmake --build build/rey --parallel 2
ctest --test-dir build/rey --output-on-failure
```

Use Windows x64, C++17, Windows SDK, .NET Framework 4.8 and PowerShell. The current
experimental native build uses LLVM-MinGW; full MSVC/WDK integration remains a
qualification requirement. Core dependencies keep their own licenses and
repository visibility. The USB library is separate:
[Pi5-AUSB](https://github.com/danrey-bilo/Pi5-AUSB).

| Directory | Responsibility |
|---|---|
| `src/service` | Settings validation/persistence, route/session lifecycle, local IPC |
| `src/engine` | Shared PCM block contract and separate LAN/USB data paths |
| `src/bridge` | Versioned bounded PCM exchange with the ACX driver |
| `apps/ReyAudioControl` | WPF layout, view state, named-pipe client, tray |
| `drivers/Acx` | Root ownership, dynamic child, circuits, WaveRT streams |
| `tests`, `tools` | Contracts, finite real-device checks, builds and packaging |

The local control pipe `ReyAudio.Control.v1` has one server instance, rejects
remote clients, and permits SYSTEM, administrators and interactive users. Only
fixed commands and validated values are accepted; it cannot execute shell
commands or select arbitrary files. Requests are below 1024 bytes, replies below
32768 bytes, and pending I/O is cancelled/drained on stop or bounded client wait.
The client acknowledges reading a reply before disconnect. Audio PCM does not
flow through this pipe, and audio callbacks do not acquire the manager mutex.

The Pi laboratory gadget uses its own lock, not the production shared LAN/USB
hardware lease. The Windows manager serializes its own sessions, but other host
applications and a future ADC/DAC backend still need the common Pi ownership and
clock design. Direct kernel USB-to-ACX transport is future work; this preview uses
WinUSB in the service.

## Evidence

Seven service contract tests pass: defaults, bounded integers, invalid profiles,
route priority, profile isolation, JSON escaping and IPv4 validation. The real
Pi manager checks cover missing-driver status, discovery during exclusive USB
streaming, inactive LAN edits, malformed commands, three format changes and USB
pause/restart. Actual WPF pages are rendered from the running service state.
See [evidence](evidence/rey-manager.json) and [USB reconnection](evidence/rey-hotplug.json).

The USB transport passed all 18 digital formats. Its earlier 299-second
192kHz/32-bit/8x8 run measured digital RTT p50/p95/p99/max
**373/410/419/944.2 us**, with no invalid packets, missing frame indices or host
wait timeouts. The report identifies the exact measured binaries; the later
branding/hotplug build has separate evidence and is not substituted for that
measurement. [USB report and raw evidence](https://github.com/danrey-bilo/Pi5-AUSB/blob/main/docs/RESULTS.md).

No measured value above is an analog or Windows audio-engine latency. There is
no promise that the 125us bus interval is end-to-end latency. No Pi4 code/image
was updated in this work.
