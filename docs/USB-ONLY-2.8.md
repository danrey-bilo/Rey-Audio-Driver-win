**English** | [Русский](USB-ONLY-2.8.ru.md)

# Rey Audio Driver 2.8: one USB device

The Windows software now uses only Pi5-AUSB and one attached Rey Audio USB
board. The 2.8.0 USB preview was built and upgraded on the Windows/Pi5 bench
on 2026-10-03. The USB-only conversion passed its build, installation, settings
and PCM-format checks. Nominal frame cadence and consistently sub-2-ms RTT
remain unqualified.

## Product and source

The active build contains no AoIP library, Winsock dependency, UDP, IOCP,
network discovery, firewall rules or LAN profile. Former sources, installer
and LAN reports are preserved in `archive/`, outside the build. The AoIP
repositories, Pi5-AUSB protocol and Pi firmware were not changed.

The service owns one `DeviceSession` and one USB interface path. There is no
card catalog or selected-card command. One board connects automatically.
An additional board does not interrupt an already attached board; a warning
is shown. Two boards present at startup are ambiguous: the service requests
one board instead of choosing arbitrarily. Enumeration policy tests simulate
these cases; the physical bench has one Pi5.

The panel contains **Mixer / USB / Diagnostics**, with a single device label.
It retains eight input and eight output channels, Master, Mute/Solo/polarity
and real PCM meters. USB and ASIO buffers remain manual. The ASIO `DeviceId`
preference was removed; internal ID validation still protects session ownership.

Source modules are `src/asio`, `src/engine`, `src/audio`, `src/service` and
`src/platform`; WPF is in `apps/ReyAudioControl`. PCM alignment and the USB
profile no longer depend on AoIP headers. [Architecture](ARCHITECTURE.md).

## Installation and migration

`Rey-Audio-USB-ASIO-Setup-x64.exe` installs four checked payloads: service,
ASIO DLL, panel/tray and native diagnostic host. It registers **Rey Audio USB
ASIO** and `ReyAudioService --service --asio-only`, with automatic service
and tray startup. UAC is required. No INI, new SYS/CAT, test certificate or
boot/security setting change is needed. Microsoft WinUSB handles the device;
TAG is not required. [Installation](USB-ASIO.md).

The local upgrade succeeded. All four installed hashes matched the manifest,
with file version 2.8.0.0. The service was Running with Auto start and x64
COM/ASIO registration pointed to the installed DLL. SCM image path and process
name were verified; the LocalSystem process executable path was unavailable
to the non-administrator read.

`HKLM\Software\ReyAudio\UsbProfile` is a single 168-byte version-3 binary
value containing USB and mixer settings. The attached board's USB profile
and mixer survived the upgrade. Legacy formats 1/2 are read for migration;
LAN fields are discarded. Saved ASIO preferences remained block64/lead3.
Tests temporarily overrode lead/depth and restored USB settings. There is
no automatic tuning.

Installer SHA256:
`333592576971e0f8c37bb5ab87205a7f824c088960133de690f93702ef9fe50b`.
The USB-ASIO preview is a separate release from the historical ACX 2.7
package; old release assets are preserved.

## Exact-build validation

[Machine-readable summary and raw suite references](evidence/rey-usb-only-20261003.json).

| Check | Result |
|---|---|
| Native contracts | 32/32: IPC ownership/timeline, profile migration/corruption, mixer, PCM alignment, endpoint mapping and single-device policy |
| Retired/invalid commands | Eleven rejected; profile and mixer unchanged |
| USB pause/resume | Same device ID became ready after resume; settings restored |
| Build graph and service PE imports | No AoIP, Winsock, IP Helper, former session engine or IOCP receiver |
| Installed WPF render | Three pages at 1120×800; eight strips and Master visible; no LAN navigation or card selector |
| Initial 18-format matrix, lead3 | 14/18 PCM checks passed in 95.968 s; four failures preserved |
| 18-format matrix, block64/lead4/depth3 | 18/18 PCM checks passed in 95.875 s; five seconds per profile; original profile restored |

Both matrices used 8×8, six rates (44.1/48/88.2/96/176.4/192 kHz) and
PCM16/packed24/int32. The initial matrix used blocks16/32/64 according to
rate. Failures were 44.1 kHz/16-bit, 176.4 kHz/24-bit and 192 kHz/16-bit/32-bit.
The second matrix verifies short PCM-format operation with greater render
reserve; it is not five-minute qualification of every profile.

All audio tests used the installed ASIO COM DLL, SCM service, native
`ReyAsioProbe` host and Pi **digital-loopback**. Actual Ableton was not observed
after this refactor. Every long audio run requested 295 seconds.

## 192 kHz / PCM32 / 8×8 timing

| Profile | Duration | Capture drops / render late / render missing frames | Digital RTT p50 / p95 / p99 / max, ms | Observed frames/s |
|---|---:|---:|---|---:|
| block64 / lead3 / USB depth3 | 295.015 s | 58 / 0 / 3712 | 1.2661 / 1.4903 / 1.6450 / 2.2682 | 189,305.6 |
| block64 / lead4 / USB depth3 | 295.008 s | 0 / 0 / 0 | 1.6286 / 1.8837 / 2.0103 / 3.5171 | 187,983.7 |
| block64 / lead4 / USB depth4 | 10.011 s | 1 / 0 / 64 | 1.7425 / 1.8534 / 2.0724 / 2.3545 | 190,791.7 |

Lead4/depth3 matched all 14,440 returned markers, with zero render overflow,
backward positions or MMCSS failures. ASIO connection ID, PID and USB
generation stayed unchanged. Host callback gap maximum was 2.5041 ms;
processing maximum 98.6 µs; capture age maximum 2.5111 ms. Host CPU was
5.71875 CPU seconds, 1.939% of one logical core. Lead3 recorded
18.6348 ms / 88.9 µs / 1.3155 ms, 10.609375 CPU seconds and 3.596% of one core.
Service CPU was not measured. Service gap maxima 1.3076/1.1603 ms may include
earlier activity within the same USB generation, unlike the host counters.

Lead4/depth3 passed PCM continuity but ran **2.09% below nominal frame rate**.
Lead3 was 1.40% below nominal and dropped frames. The short depth4 experiment
was closer to nominal cadence but failed continuity. The cause is not
established; Windows scheduling, USB completions and digital backend pacing
need separate tracing. A physical audio clock was not measured.

ASIO reports a buffer model: `input = N + depth × ceil(rate/8000)` and
`output = (lead−1) × N + 1`. Block64/lead3/depth3 reports **1.3802 ms**;
lead4/depth3 reports **1.7135 ms**. These values do not replace measured RTT
or analog loopback. The new build has not qualified a 32/16-sample minimum
or consistently sub-2-ms RTT.

## Repeat and remaining work

Close the DAW; use one board with a unity mixer for native marker checks:

```powershell
python tools/test_installed_usb_asio.py --bin 'C:/Program Files/ReyAudio/USBASIO' `
  --out build/proof-formats --block 64 --lead 4 --depth 3
python tools/test_usb_asio_profile.py --bin 'C:/Program Files/ReyAudio/USBASIO' `
  --out build/proof-profile --seconds 295 --block 64 --lead 4 --depth 3
powershell -NoProfile -File tools/verify_usb_install.ps1 `
  -SetupDirectory build/usb-setup -Output build/install-check.json
```

The USB-only/single-board conversion is delivered. Next are nominal frame
cadence and deadline stability, actual Ableton, then physical ADC/DAC loopback.
Chrome/Telegram Windows endpoints remain a separate backend; this USB-ASIO
EXE does not create them. Removal and a full physical hotplug cycle still
need qualification. Earlier 2.7 Ableton results are historical evidence,
not acceptance of the new binary hashes.
