**English** · [Русский](RELEASE-2.8.1.ru.md)

# Rey Audio Driver 2.8.1 USB preview

Tag **v2.8.1-usb-preview.1** · one Pi5 · Windows x64 · USB-ASIO 8×8.

This update improves USB receive rearming and documents manual 96/192 kHz
low-latency profiles with exact installed-build evidence. Pi5-AUSB 0.1.1 adds
FIFO40 and memory locking on the test board. ASIO settings, live state and
reported buffer latency are clearer in the panel. The installer skips unchanged
payloads while retaining lock checks and rollback for changed files.

Windows contracts **32/32**, transport **7/7 on Windows and Pi5**, installed
SDK 0.1.1 EXACT consumer **1/1**, final PCM format matrix **6/6**. Native profiles
192/64/lead3/depth4 and 96/32/lead3/depth4 ran **175 s each with zero PCM/deadline
errors**. RTT p99: **1.5280 / 1.5199 ms**; max: **4.3675 / 3.1292 ms**.
Both final-code Ableton profiles passed **175 s each at CPU Usage Simulator 50%**
with zero drops/late/missing/overflow. ASIO reports **1.5052 / 1.5104 ms**.
Strict maximum below3ms is still open. Actual Ableton observations and rejected
profiles remain in the [report](USB-LATENCY-2.8.1.md). Physical ADC/DAC is unmeasured.

| Download | Contents |
|---|---|
| `Rey-Audio-USB-ASIO-2.8.1-preview.1-x64.exe` | UAC installer; ASIO, automatic service, mixer/tray |
| `Rey-Audio-USB-ASIO-2.8.1-preview.1-x64.zip` | Same installer, EN/RU docs, screenshots and evidence |
| `Rey-Audio-USB-ASIO-2.8.1-preview.1-source.zip` | Committed source, without external SDK headers/binaries |
| `SHA256.json` | Embedded payload hashes and installer hash |
| `SHA256SUMS-2.8.1-preview.1.txt` | Download checksums |

[Downloads](https://github.com/danrey-bilo/Rey-Audio-Driver-win/releases/tag/v2.8.1-usb-preview.1) · [Install](USB-ASIO.md) · [Raw evidence](evidence/rey-usb-latency-281-20261003.json).
No custom kernel driver, certificate, Test Mode, INI or boot/security change.
Ordinary Windows audio endpoints, physical hotplug/sleep/resume and uninstall
qualification remain separate stages. AoIP/Pi4 and previous release assets are unchanged.
