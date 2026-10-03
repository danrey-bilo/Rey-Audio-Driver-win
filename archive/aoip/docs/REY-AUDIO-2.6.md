**English** | [Русский](REY-AUDIO-2.6.ru.md)

# Rey Audio Driver 2.6.1 preview

One Windows background service manages separate USB and AoIP/LAN transports.
The WPF panel opens on **Mixer**, with eight input and eight output controls.
Service and panel build without the ASIO SDK. One Raspberry Pi 5 is supported.

## Components

| Component | Responsibility |
|---|---|
| `ReyAudioService.exe` | Automatic SCM service, USB discovery/reconnect, transport profiles, live mixer and one active session |
| `ReyAudioControl.exe` | Mixer, separate USB/LAN pages, manual settings, real status, diagnostics and tray |
| USB | 8×8 PCM16/24/32; 44.1, 48, 88.2, 96, 176.4, 192 kHz; HS interrupt and asynchronous WinUSB |
| AoIP | UDP/IPv4/IOCP engine, manual block/guard/packet frames, optional silence suppression |
| ACX/KMDF | Own root bridge and dynamic input/output child, bounded versioned PCM exchange |
| EXE / MSI | Readiness checks, unique local test signing, own root installation, service, tray and uninstall |

The kernel package is built and checked but **has not been loaded or exercised
through WASAPI**. Local signing is verified independently. The Pi USB backend is
digital loopback; independent hardware clock/feedback, ADC/DAC and physical
latency remain open. The panel reports an unavailable driver explicitly.

## Install and use

Use the [EXE / MSI installation guide](INSTALL.md). There are no INI files or
PowerShell installation steps in this package. Windows Test Mode is needed for
the locally signed kernel driver; the EXE offers a separate explicit preparation
button. It does not disable Secure Boot/Core Isolation or restart the PC.

USB interface enumeration runs once per second without opening its exclusive
stream handle. Opening checks the stable DeviceId and HS descriptor. Automatic
discovery remains available while streaming. Software gadget re-enumeration was
tested separately; a power-losing physical unplug and Windows sleep/resume were
not qualified.

For LAN, open the tray → **AoIP / LAN**, discover or enter the Pi IPv4 address,
then connect. Configured LAN has priority by default; **Use USB** explicitly
selects USB. A switch drains the old session before opening the new one and
interrupts streaming. Automatic LAN-to-USB failover is not implemented: the
requested LAN session retries until the user disconnects LAN or selects USB.

USB, LAN and mixer state are stored atomically in the 64-bit registry value
`HKLM\Software\ReyAudio\Profiles` (bounded binary schema RYS1/v2). Older schema
v1 is read with a unity mixer. Editing an inactive profile or mixer controls does
not restart the active transport. Invalid commands leave state unchanged. There
is no automatic buffer tuning. The default LAN block256/guard1536/packet32 is a
conservative starting profile from earlier digital tests, not a latency promise.
LAN rate/bits come from the Pi; USB negotiates its own rate/bits.

The installer creates a service-only Private/LocalSubnet UDP50021 firewall rule.
The SYSTEM service updates only its own rule when the local port changes;
missing/inaccessible rules appear in diagnostics. A finite console test never
changes system firewall rules. The panel runs as an ordinary interactive user,
closes to the tray and can exit without stopping the service.

[Mixer controls, processing and the <2 ms physical USB target](MIXER.md).

## Build and structure

```powershell
cmake -S . -B build/rey -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DPIAOIP_BUILD_ASIO=OFF -DPIAOIP_ENABLE_USB=ON `
  -DAOIP_SOURCE_DIR=C:/work/AoIP-lib -DPI5AUSB_SOURCE_DIR=C:/work/Pi5-AUSB
cmake --build build/rey --parallel 2
ctest --test-dir build/rey --output-on-failure
```

Windows x64, C++17, Windows SDK, .NET Framework 4.8 and PowerShell are required.
The current experimental native/ACX build uses LLVM-MinGW / Clang with MSVC ABI;
supported MSVC/WDK qualification remains open. Build ACX separately with
`tools/build_acx.py` using its SDK/WDK arguments. Runtime users do not need these
tools. Build the MSI/EXE using official WiX 3.14.1:

```powershell
./tools/build_setup.ps1 -Bin build/rey/bin -Driver build/acx `
  -Wix C:/tools/wix3141 -Output build/setup
./tools/verify_setup.ps1 -Setup build/setup -Bin build/rey/bin `
  -Driver build/acx -Wix C:/tools/wix3141 -Output build/setup-inspection
```

`build_setup.ps1` compiles the fixed driver hashes into the signing helper and
runs WiX ICE validation. `verify_setup.ps1` opens the MSI read-only, extracts its
CAB, compares all payload hashes and checks that the EXE embeds that exact MSI.
The installer has no distributed private key. Its built-in Windows signer needs
neither SignTool nor an internet connection on the client.

| Directory | Responsibility |
|---|---|
| `src/service` | Registry codec, validation, routing/lifecycle, bounded local IPC |
| `src/audio` | Allocation-free eight-channel gain/mute/solo/polarity DSP and meters |
| `src/engine` | Shared PCM block contract, separate LAN and USB engines |
| `src/bridge` | Versioned bounded PCM exchange with ACX |
| `src/platform` | Windows scheduling and own firewall port management |
| `apps/ReyAudioControl` | WPF layout, channel view model, pipe client, tray |
| `apps/ReyAudioSetup`, `installer` | Local signing, narrow device ownership, MSI and EXE |
| `drivers/Acx` | Root ownership, dynamic child, circuits and WaveRT streams |
| `tests`, `tools` | Contracts, finite device checks, builds and packaging |

The `ReyAudio.Control.v1` pipe rejects remote clients, accepts SYSTEM,
administrators and interactive users, and executes only fixed validated
commands. Requests are under 1024 bytes and replies under 32768 bytes. PCM does
not travel through this pipe. Stop cancels/drains pending I/O; audio callbacks do
not acquire the manager mutex. Mixer controls publish atomically without adding
an audio queue; their gain transition is 64 samples.

Finite tests use isolated binary `.dat` settings with the same codec, not the
system registry. `--digital-test` requires explicit finite console mode; normal
SCM startup cannot silently replace an unavailable ACX with loopback.

The Pi laboratory gadget has its own lock. A production shared LAN/USB hardware
lease and independent clock design still need a connected audio backend. Direct
kernel USB-to-ACX transport is future work; this build uses WinUSB in the service.

## Evidence

- **14/14** checks: nine service contracts (including persistence/corruption)
  and five DSP checks (PCM16/24/32 unity, solo/mute, gain/clipping, ramp and bounds).
- **10/10** finite real-Pi service checks on the exact packaged service:
  discovery, explicit missing-driver state, inactive LAN edits, live mixer,
  malformed commands, three formats and pause/reconnect. [JSON](evidence/rey-manager-msi.json).
- The earlier mixer timing run has its own exact service hash:
  [JSON](evidence/rey-manager-mixer.json). Its measurements are not relabelled as
  the final firewall-enabled service.
- SYS/INF/CAT validation, MSI ICE validation, CAB/hash inspection, free local
  signing, catalog membership and damaged-SYS rejection passed.
  [Installer evidence](evidence/rey-installer.json).
- Actual WPF renders cover all four pages, eight visible channel controls and
  service state. The screenshot does not prove installed Windows endpoints.

The earlier USB library 299-second 8×8/192kHz/PCM32/depth3 test measured digital
RTT p50/p95/p99/max **373/410/419/944.2 µs**, with zero invalid packets, missing
frame indices or host timeouts. The USB report identifies the measured binaries
separately from later builds. [Report](https://github.com/danrey-bilo/Pi5-AUSB/blob/v0.1.0/docs/RESULTS.md).

Elevated MSI install/repair/uninstall, loaded ACX, Driver Verifier/cancellation,
WASAPI/DAW, independent-clock audio and physical ADC/DAC loopback remain required.
Audio runs are finite and capped at 300 seconds. The 125 µs bus interval is not
an end-to-end audio measurement. Pi4 is unchanged.
