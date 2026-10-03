# Rey Audio USB ASIO 2.8

One USB device, eight inputs and eight outputs, PCM16/24/32 and six rates
44.1–192 kHz. The active Windows driver no longer includes AoIP/LAN, a
multi-card catalog or a selectable ASIO card binding.

Run **Rey-Audio-USB-ASIO-Setup-x64.exe** and accept UAC. It installs the user-mode
ASIO DLL, automatic service and desktop/tray panel in
`%ProgramFiles%\ReyAudio\USBASIO`. Microsoft WinUSB handles USB. No custom SYS,
certificate, Test Mode or boot/security setting change is needed. TAG is not
required. This package does not create system microphones/speakers for ordinary
Windows apps; that is the [separate endpoint stage](ENDPOINTS.md).

Connect one Pi5 over USB; detection is automatic. `ReyAudioControl.exe` provides
Mixer / USB / Diagnostics. One ASIO host owns the card. USB settings are manual.
Start testing with 192000 Hz, PCM32, USB depth 3 and ASIO block 64. Consult the
[exact-build report](USB-ONLY-2.8.md) before selecting render lead 3/4.

In Ableton Settings → Audio:

| Field | Value |
|---|---|
| Driver Type | ASIO |
| Audio Device | Rey Audio USB ASIO |
| In/Out Sample Rate | 192000 Hz for the low-latency profile |
| Buffer Size | 64 Samples for initial testing |
| Hardware Setup | Rey panel, USB → ASIO settings |

After Save ASIO, reopen the driver in Ableton. Stop the host before changing
USB rate/bits/depth, apply the profile, then reopen ASIO. Reopen it after USB
loss/reappearance too. For DLL updates, fully exit Ableton: No Device can
retain the DLL. The installer checks file locks before replacement.

Reported latency is in integer frames:
`input = N + depth × ceil(rate / 8000)`, `output = (lead − 1) × N + 1`.
At 192000/64/depth3/lead3 this is 136 + 129 frames, **1.3802 ms**;
lead4 reports 136 + 193, **1.7135 ms**. This is a buffer model, not measured
analog round trip. A 125 µs USB microframe is not end-to-end audio latency.
[Buffer guide](BUFFER-GUIDE.md).

The installer verifies four payload SHA256 hashes, registers x64 COM/ASIO and
`ReyAudioService --service --asio-only`, and enables service/tray startup.
It replaces only its owned installation and rolls back replacement errors.
Earlier USB/mixer preferences migrate to one profile; retired network settings
cannot activate a transport.

Uninstall is available in Installed apps. Preferences are retained; the running
installer and log can remain in the install folder. Local installation/upgrade
are checked; uninstall is not yet qualified. This EXE is separate from the
historical 2.7 ACX MSI.

Evidence: [2.8 report](USB-ONLY-2.8.md),
[earlier digital ASIO/Ableton tests](evidence/rey-usb-asio-20261003.json),
[earlier lower-buffer trials](evidence/rey-usb-asio-low-buffer-20261003.json).
Previous build results are not qualification of 2.8. Physical ADC/DAC and
analog round-trip latency have not been measured.
