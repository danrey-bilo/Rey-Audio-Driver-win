![Rey Audio Driver](docs/assets/rey-header.svg)

# Rey Audio Driver

**English** | [Русский](README.ru.md)

Windows 11 x64 audio service and own ACX/KMDF driver for Raspberry Pi 5.
Separate **USB** and **AoIP / LAN** connections, separate manual settings,
automatic USB detection, background service and a new desktop/tray panel.

**2.6.0 preview:** [setup and architecture](docs/REY-AUDIO-2.6.md) ·
[USB transport](docs/USB-TRANSPORT.md) ·
[builds](https://github.com/danrey-bilo/Rey-Audio-Driver-win/releases).

The service and panel build without an ASIO SDK. The own audio driver is unsigned
and has not been installed or qualified through WASAPI. The USB Pi backend is
digital loopback. The panel explicitly shows driver availability; physical
ADC/DAC and production audio-endpoint qualification remain open.

![Rey USB panel](docs/assets/rey-usb.png)

| | USB | AoIP / LAN |
|---|---|---|
| Connection | Automatic USB interface discovery | Select/configure a Pi through the tray |
| Format | 8 inputs + 8 outputs, PCM16/24/32, six rates 44.1–192 kHz | Format from the configured Pi, up to 8 inputs + 8 outputs |
| Buffers | Manual transfer queue and endpoint settings | Manual block, guard and packet frames |
| Data path | High-Speed interrupt, WinUSB overlapped I/O | UDP/IPv4, IOCP receive, bounded audio deadlines |
| Priority | Explicit selection available | Default after a LAN connection is configured |

Use the extracted preview's `tools/install_rey.ps1` from an Administrator
PowerShell. It installs the automatic **Rey Audio Driver** service and tray panel,
preserves settings and verifies executable hashes. Kernel driver installation is
separate and requires a prepared signing/test environment. Read the
[installation guide](docs/REY-AUDIO-2.6.md#install-the-user-mode-components).

The USB library and Pi gadget live in their own repository:
[Pi5-AUSB](https://github.com/danrey-bilo/Pi5-AUSB). The
[AoIP core](https://github.com/danrey-bilo/AoIP-lib) remains a separate dependency.
Their visibility and license terms are unchanged.

Seven service contract checks and finite real-Pi tests cover profile isolation,
invalid requests, USB discovery while streaming, restart and reconnection.
[Evidence and limits](docs/REY-AUDIO-2.6.md#evidence).

The earlier 299-second 8x8/192kHz/32-bit USB test measured digital RTT
p50/p95/p99/max **373/410/419/944.2 us**. This is a digital transport loopback,
not analog or Windows audio-engine latency. [Full USB report](https://github.com/danrey-bilo/Pi5-AUSB/blob/main/docs/RESULTS.md).

The existing ASIO adapter and immutable [2.5.0 release](https://github.com/danrey-bilo/Rey-Audio-Driver-win/releases/tag/v2.5.0)
remain available during the transition. [Previous release notes](docs/RELEASE-2.5.0.md).
One Pi is supported; multiple Pi devices, physical audio, clock feedback and a
direct kernel USB path remain qualification work. Pi4 is not updated.

[License](LICENSE) · [Build](docs/REY-AUDIO-2.6.md#build-and-structure)
