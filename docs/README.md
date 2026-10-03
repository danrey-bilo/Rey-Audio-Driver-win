**English** · [Русский](README.ru.md)

# Rey Audio Driver documentation

Rey Audio Driver 2.8 is the USB-ASIO preview for one Raspberry Pi 5 and
8×8 audio. Start with installation, then set the mixer and manual buffers.

| I want to… | Guide |
|---|---|
| Download the Windows installer | [USB preview release](https://github.com/danrey-bilo/Rey-Audio-Driver-win/releases/tag/v2.8.0-usb-preview.1) |
| Install and select the driver in Ableton | [USB-ASIO](USB-ASIO.md) |
| Set block size, render reserve and USB queue | [Buffer guide](BUFFER-GUIDE.md) |
| Control channels and meters | [Mixer](MIXER.md) |
| Read measurements and limitations | [2.8 validation](USB-ONLY-2.8.md) · [Raw evidence](evidence/rey-usb-only-20261003.json) |
| See what changed | [Release notes](RELEASE-2.8.0.md) |
| Build the software and installer | [Build](BUILD.md) · [External ASIO SDK](ASIO-SDK.md) |
| Understand the audio path | [Architecture](ARCHITECTURE.md) · [USB transport](USB-TRANSPORT.md) |
| Integrate USB/mixer control | [Control API](API.md) |
| Check ordinary Windows audio | [Windows endpoints](ENDPOINTS.md) · [Earlier TAG experiment](GATEWAY.md) |
| Inspect retired work | [AoIP archive](../archive/aoip/README.md) · [Old installer](../archive/windows-endpoints-2.7/README.md) |

The ASIO package installs without Test Mode or a new kernel driver. It does
not create ordinary Windows microphones/speakers. Current tests use a digital
loopback, with no physical ADC/DAC qualification. Keep these boundaries when
comparing reported buffer latency and measured roundtrip.

[Back to the project](../README.md)
