# AKM board modes — development

[Русский](AKM-OPERATING-MODES.ru.md) · [Documentation](README.md)

The PC selects **two modes** in **Rey Audio Driver → Settings → Board mode**.
Development build identifier: `2.8.2-akm-dev.1` (not the published 2.8.1 preview).
The third runtime state, standalone Effects processor, is automatic on USB
disconnect. It is not a selectable PC mode and there is no Pi web mode selector.

| PC selection | Rates in this ASIO transport | Effects |
| --- | --- | --- |
| Audio interface | 44.1 / 48 / 88.2 / 96 / 176.4 / 192 kHz | Unloaded on the Pi; ADC → PC, PC → DAC |
| Combined | 44.1 / 48 / 88.2 / 96 kHz | Enabled; independent capture/return settings for eight channels |

The AKM converter/Linux application policy also permits 384 kHz in audio-interface
mode. **The current Pi5-AUSB Windows transport does not support 384 kHz.** The
service and ASIO `canSampleRate` both enforce the selected mode's rate limit.

## Channel routing

For each of eight channels choose independently:

* **To PC:** before effects (physical ADC), or the corresponding graph output.
* **From PC:** directly to DAC, or through the effects graph.

A USB return through effects replaces that graph input's ADC source. It does
not create two parallel instances of the same chain. The post-effects PC tap
therefore follows the selected source and the preset's cross-channel routing.
Unplugging selects the saved standalone profile, limited to 96 kHz. Reconnect
restores the previous PC mode, profile and channel settings.

Stop the ASIO engine before applying mode/format changes. The panel disables
these controls while ASIO runs, and the service independently rejects mode
changes. Changes use revision checks, asynchronous admission, completion polling
and state readback. A timeout is not automatically replayed as another mutation.

## Connection prerequisite and current boundary

This is a **development control integration**, not a qualified AKM USB audio
release. The panel requires an already configured **Rey Effects CDC management
function** on the Pi. It discovers one Linux gadget COM port, validates the
`Rey Effects` protocol-1 handshake and keeps PCM out of the management channel.
Without that function the controls remain unavailable with an explanation.

The companion Linux application provides the mode engine and JACK port contract,
plus an optional adapter for an already configured ALSA UAC2 8×8 gadget. UAC2
and the vendor Pi5-AUSB WinUSB audio protocol are different transports. The
existing Pi5-AUSB gadget is a digital-loopback backend; this change **does not
connect it to AKM PCM, configure a composite USB gadget or prove duplex USB
audio through the board**. A working ASIO → AKM bridge and physical qualification
are still required. Do not substitute a successful control command for audio proof.

Companion source: [Rey Effects Linux ARM](https://github.com/danrey-bilo/Rey-Effects-Linux-arm),
branch `codex/operating-modes` (repository access required).

## Service and saved settings

`BOARD_MODE soundcard|combined` stores the local policy. Selecting Combined
clamps a previously saved high rate to 96 kHz. A later high-rate USB or ASIO RATE
command is rejected. `STATUS.board_mode` reports the saved policy; it is not a
remote Pi acknowledgement. The panel confirms the Pi first, then updates this
local policy. The read-only command `ASIO CAN_RATE <identity> <rate>` queries it.

Saved USB/mixer settings now use **format 4** (172 bytes), preserving the existing
PCM/mixer fields and adding the board-mode flag. Formats 1, 2 and 3 migrate with
Audio interface as the backward-compatible default. Older service binaries
cannot read format 4: preserve the old registry/profile backup before downgrading.
The ASIO shared-memory ABI remains version 1.

## Validation, 2026-10-15

* Service, ASIO DLL and probe compiled on Windows x64 with C++17/LLVM-MinGW.
* **33/33 native CTests passed**, including installed-v3 settings migration,
  mode persistence, high-rate rejection and prior IPC/mixer contracts.
* **21 C# contracts passed** for two choices, rate ranges, eight ordered routes,
  revision fields and invalid command rejection.
* Real WPF binding/render fixture checked two modes and eight channel rows;
  it did not change the installed service or enumerate a physical USB board.
* The Pi's actual CDC dispatcher passed eight requests through a test-owned PTY.
  The PTY is simulated transport; it does not qualify Windows serial/USB hardware.

```powershell
./tools/build_control.ps1
./tools/check_board_control.ps1
cmake --build build/usb
ctest --test-dir build/usb --output-on-failure
```

The new Windows workflow compiles/tests the WPF control code without downloading
the external ASIO SDK. Full native checks above were run separately with the SDK.
No production installer/release or upgrade of the installed Windows service is
claimed by this source change. See the companion Linux report for meter and
mode-transaction measurements.
