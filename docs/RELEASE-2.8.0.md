**English** · [Русский](RELEASE-2.8.0.ru.md)

# Rey Audio Driver 2.8.0 USB preview

Tag: **v2.8.0-usb-preview.1** · Windows x64 · one USB device · 8×8.

This release converts Rey Audio Driver to USB-only operation. The service,
ASIO frontend and mixer use one Pi5-AUSB board. Network transport and
multiple-card management are retired; existing AoIP releases are preserved.

## Changes

- One USB session with automatic discovery and stable physical identity.
- Mixer, USB and Diagnostics pages; no LAN page or card selector.
- PCM16/packed24/int32, 8 inputs and 8 outputs, six rates from 44.1 to 192 kHz.
- Manual ASIO block/render reserve and USB queue.
- Atomic USB/mixer profile with migration from older settings.
- Standalone EXE for ASIO, automatic Windows service and mixer/tray.
- No custom kernel driver, INI, test certificate or Test Mode requirement.
- Active build independent of AoIP-lib, Winsock, IOCP and firewall configuration.

## Download

| Asset | Contents |
|---|---|
| `Rey-Audio-USB-ASIO-2.8.0-preview.1-x64.exe` | Direct installer; confirm UAC |
| `Rey-Audio-USB-ASIO-2.8.0-preview.1-x64.zip` | Same installer, EN/RU docs, screenshots, validation evidence and license notices |
| `Rey-Audio-USB-ASIO-2.8.0-preview.1-source.zip` | Complete project source for this tag; external SDK headers/binaries excluded |
| `SHA256.json` | Four embedded payload hashes and installer hash |
| `SHA256SUMS-v2.8.0-usb-preview.1.txt` | Release download checksums |

[Release downloads](https://github.com/danrey-bilo/Rey-Audio-Driver-win/releases/tag/v2.8.0-usb-preview.1)
· [Installation and Ableton](USB-ASIO.md).

## Validation and remaining work

32/32 native contracts passed. The installed COM driver passed all 18 PCM
formats with block64/lead4/USB depth3. Its 295-second 192 kHz/PCM32 run had
zero capture drops or render late/missing frames. Digital RTT p50/p95/p99/max:
**1.6286 / 1.8837 / 2.0103 / 3.5171 ms**; frame cadence was **2.09% below
nominal**. Earlier failed profiles remain in the evidence.

**Pre-release:** nominal cadence, consistently sub-2-ms RTT, actual Ableton
after the refactor, physical ADC/DAC, removal and physical hotplug still need
qualification. Windows audio endpoints for Chrome/Telegram are a separate
stage. Tests used the Pi digital-loopback backend.

[Exact-build report](USB-ONLY-2.8.md)
· [Machine-readable evidence](evidence/rey-usb-only-20261003.json).
