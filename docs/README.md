**English** · [Русский](README.ru.md)

# Rey Audio Driver documentation

Rey Audio Driver 2.8.1 is the USB-ASIO preview for one Raspberry Pi 5 and
8×8 audio. The current 2.8.2 development branch adds AKM USB/JACK integration
and fixed per-rate transport tuning. Start with installation, mixer and ASIO buffer.

| I want to… | Guide |
|---|---|
| Download the Windows installer | [USB preview release](https://github.com/danrey-bilo/Rey-Audio-Driver-win/releases/tag/v2.8.1-usb-preview.2) |
| Install and select the driver in Ableton | [USB-ASIO](USB-ASIO.md) |
| Set the ASIO buffer | [Buffer guide](BUFFER-GUIDE.md) |
| Read the current AKM USB digital qualification | [2026-10-16 results](USB-LATENCY-2026-10-16.md) |
| Control channels and meters | [Mixer](MIXER.md) |
| Select AKM audio-interface / combined mode | [Development mode controls and USB prerequisites](AKM-OPERATING-MODES.md) |
| Read measurements and limitations | [96/192 kHz validation](USB-LATENCY-2.8.1.md) · [Raw evidence](evidence/rey-usb-latency-281-20261003.json) |
| See what changed | [Release notes](RELEASE-2.8.1.md) |
| Build the software and installer | [Build](BUILD.md) · [External ASIO SDK](ASIO-SDK.md) |
| Understand the audio path | [Architecture](ARCHITECTURE.md) · [USB transport](USB-TRANSPORT.md) |
| Integrate USB/mixer control | [Control API](API.md) |
| Check ordinary Windows audio | [Windows endpoints](ENDPOINTS.md) · [Earlier TAG experiment](GATEWAY.md) |
| Inspect retired work | [AoIP archive](https://github.com/danrey-bilo/Rey-Audio-Driver-win/blob/18e6353fa767dcbcde1cef15dabab339848e62d1/archive/aoip/README.md) · [Old installer](https://github.com/danrey-bilo/Rey-Audio-Driver-win/blob/18e6353fa767dcbcde1cef15dabab339848e62d1/archive/windows-endpoints-2.7/README.md) |

The dev.7 package embeds the original signed SAR endpoint kernel and creates
four Windows stereo pairs in each direction automatically. No external routing
application is needed. See the [signature-policy limits](ENDPOINTS.md).
Current tests use a digital loopback, with no physical ADC/DAC qualification. Keep these boundaries when
comparing reported buffer latency and measured roundtrip.

[Back to the project](../README.md)

[Interface update and Pi cleanup](USB-PREVIEW2-20261004.md)
