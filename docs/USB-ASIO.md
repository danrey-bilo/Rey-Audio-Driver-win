**English** · [Русский](USB-ASIO.ru.md)

# Install Rey Audio USB ASIO 2.8.1

One USB board, eight inputs and eight outputs, PCM16/24/32, 44.1–192 kHz.
Run **Rey-Audio-USB-ASIO-2.8.1-preview.2-x64.exe** and accept UAC. It installs
the x64 user-mode ASIO DLL, automatic service and mixer/tray panel under
`%ProgramFiles%\ReyAudio\USBASIO`. Microsoft WinUSB handles the USB interface.
No custom SYS, test certificate, Test Mode, INI or boot/security change is needed.
System microphones/speakers for Chrome/Telegram are a [separate stage](ENDPOINTS.md).

Connect one configured Pi5-AUSB board; USB detection is automatic. Open
**Rey Audio Driver → Settings (Настройки)**. The Pi needs runtime 0.1.1 for the documented tests.
The package allows one ASIO host at a time and retains saved manual settings.

## Ableton settings

| Field | Observed Ableton profile |
|---|---|
| Driver Type | ASIO |
| Audio Device | Rey Audio USB ASIO |
| In/Out Sample Rate | 192000 Hz or 96000 Hz |
| Buffer Size | 64 Samples at 192 kHz; 32 at 96 kHz |
| Hardware Setup | Rey → Settings → ASIO |
| ASIO render reserve | 3 blocks; test the intended project |
| USB format | PCM32, queue4 |

Check the [measured profiles and actual Ableton observations](USB-LATENCY-2.8.1.md)
before treating a small block as stable. Increase reserve manually if deadline
counters rise. USB depth and ASIO block/reserve are separate controls.

1. Select **No Device** in Ableton before changing USB rate/bits/depth.
2. Apply the USB format in Rey. Select ASIO block/reserve and **Save** on the ASIO card.
3. Select **Rey Audio USB ASIO** again. Verify the displayed rate and Buffer Size.

The panel shows live settings and selected settings separately. Saving changes
the next ASIO opening. Reopen ASIO after USB loss/reappearance or format changes.

Reported latency uses integer frames:
`input = N + depth × ceil(rate / 8000)`, `output = (lead − 1) × N + 1`.
At depth4/lead3, 192 kHz/64 reports **1.5052 ms** and 96 kHz/32 reports
**1.5104 ms**. These are buffer calculations. Digital RTT has occasional peaks
above 3 ms; physical ADC/DAC latency remains unmeasured. A 125 µs microframe
does not imply 125 µs end-to-end audio. [Buffer guide](BUFFER-GUIDE.md).

## Update and remove

Fully exit Ableton before a driver-DLL update: No Device can retain the DLL.
The installer checks all payload hashes and replaces only changed owned files.
A byte-identical DLL need not be rewritten during a service/panel update.
An update of the final service/panel with the unchanged DLL loaded was checked
locally; changes to a loaded DLL remain blocked until the host exits.

The installer registers COM/ASIO and `ReyAudioService --service --asio-only`,
enables service/tray startup, preserves preferences and rolls back replacement
errors. It migrates previous USB/mixer settings without activating network transport.
Uninstall is available in Installed apps; preferences are retained. Installation/
upgrade passed locally, while uninstall and physical hotplug are not yet qualified.

[Release](https://github.com/danrey-bilo/Rey-Audio-Driver-win/releases/tag/v2.8.1-usb-preview.2)
· [Exact-build report](USB-LATENCY-2.8.1.md) · [2.8.0 history](USB-ONLY-2.8.md).

The panel starts automatically after installation and at login, with its window
hidden. Its icon appears when the service detects the board; Windows may place
it in the notification-area overflow. [Preview 2 changes](USB-PREVIEW2-20261004.md).
