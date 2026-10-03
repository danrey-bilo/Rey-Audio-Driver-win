![Rey Audio Driver](docs/assets/rey-header.svg)

# Rey Audio Driver

**English** | [Русский](README.ru.md)

Windows 11 x64 audio service and own ACX/KMDF driver for Raspberry Pi 5.
The **Mixer** home page provides eight input and eight output channel controls.
**USB** and **AoIP / LAN** have separate pages and manual profiles; USB is
detected automatically, while LAN is configured through the tray panel.

**2.6.1 preview:** [EXE / MSI downloads](https://github.com/danrey-bilo/Rey-Audio-Driver-win/releases/tag/v2.6.1-preview.1) ·
[Installation](docs/INSTALL.md) · [Mixer and latency](docs/MIXER.md) ·
[Architecture and build](docs/REY-AUDIO-2.6.md).

![Rey Audio mixer](docs/assets/rey-mixer.png)

| | USB | AoIP / LAN |
|---|---|---|
| Connection | Automatic USB interface discovery | Select/configure a Pi through the tray |
| Format | 8 inputs + 8 outputs, PCM16/24/32, six rates 44.1–192 kHz | Format from the configured Pi, up to 8 inputs + 8 outputs |
| Buffers | Manual transfer queue and audio block settings | Manual block, guard and packet frames |
| Data path | High-Speed interrupt, asynchronous WinUSB | UDP/IPv4, IOCP receive, bounded audio deadlines |
| Priority | Explicit selection available | Default after a LAN connection is configured |

Run **Rey-Audio-Setup-2.6.1-preview.1-x64.exe**. It embeds the MSI and checks
Windows compatibility. On a PC already in Test Mode, installation uses the normal
UAC prompt. Otherwise the EXE offers an explicit Test Mode preparation step;
restart Windows yourself and then install. Secure Boot is changed manually in
BIOS when required. MSI does not change Core Isolation or reboot the PC.
[Step-by-step instructions](docs/INSTALL.md).

No paid certificate or external signing tools are needed on the installation
PC. The installer creates a unique local test certificate, signs SYS/CAT, trusts
its public certificate on this PC, and deletes the temporary private key.
Windows still requires Test Mode for this experimental kernel driver.
Settings are stored by the service in the registry; the new package needs no INI
or installation scripts.

The mixer has gain, Mute, Solo, polarity and master output controls, real PCM
peak meters and clipping counters. It processes the current callback without an
extra PCM queue. Fourteen contract/DSP tests and ten finite real-Pi service
checks passed. Local signing, catalog membership and rejection of a damaged SYS
also passed. [Evidence and remaining checks](docs/REY-AUDIO-2.6.md#evidence).

The earlier 299-second 8×8/192kHz/32-bit USB run measured digital RTT
p50/p95/p99/max **373/410/419/944.2 µs**. Physical audio roundtrip below **2 ms**
is the target; installed ACX/WASAPI, an independent hardware clock and ADC/DAC
loopback have not qualified it. This is a **Pre-release**.
[USB measurements](https://github.com/danrey-bilo/Pi5-AUSB/blob/v0.1.0/docs/RESULTS.md).

The separate [Pi5-AUSB](https://github.com/danrey-bilo/Pi5-AUSB) and
[AoIP-lib](https://github.com/danrey-bilo/AoIP-lib) dependencies retain their
visibility and license terms. The service and panel build without an ASIO SDK.
The immutable [ASIO 2.5.0 release](https://github.com/danrey-bilo/Rey-Audio-Driver-win/releases/tag/v2.5.0)
remains available during the transition. One Pi is supported; multiple Pi
devices are deferred. Pi4 is not updated.

[License](LICENSE) · [USB transport](docs/USB-TRANSPORT.md) ·
[Release notes](docs/RELEASE-2.6.1.md)
