# Windows stereo inputs and outputs

**2.8.2-akm-dev.7** integrates the original signed [Synchronous Audio Router 0.13.1](https://github.com/eiz/SynchronousAudioRouter/releases/tag/v0.13.1) kernel. One Rey installer automatically creates four stereo recording devices and four stereo playback devices: **Input / Output 1-2, 3-4, 5-6 and 7-8 (Rey Audio)**. No external routing application, SAR ASIO wrapper or network filter is installed.

Rey's mixer controls all channels. MONO/STEREO links share controls while preserving distinct left/right samples. Direct ASIO keeps all eight channels each way and can run alongside Windows audio. Windows and ASIO playback are added before the output mixer, with saturating PCM addition.

## Audio path

Windows application → signed WaveRT pair → Rey shared-memory bridge → mixer → custom AUSB transport → Pi. Recording takes the reverse path. The service owns USB and follows the native hardware clock for both APIs. With no Windows/ASIO streams, USB audio idles; management remains available.

Pairs use stereo PCM32 at the current USB rate; USB may use PCM16/24/32. Supported rates are **44.1 / 48 / 88.2 / 96 / 176.4 / 192 kHz**, with Combined limited to 96 kHz. Change the rate in Rey. SAR uses different Windows endpoint IDs per rate; reopen affected applications after changes. ASIO identity is unchanged.

The Windows shared engine reported a **10 ms period** on this bench. Sub-5-ms ASIO digital RTT does not establish Windows shared-audio or analog ADC/DAC latency. ASIO bypasses that engine.

## Installation and limits

The original SYS/INF/CAT, their hashes, notices and corresponding source archive are bundled unchanged. Only `ROOT\REYAUDIO_SAR\0000` is owned. Another SAR root blocks takeover. The installer prepares dormant third-party `EP` property stores through SetupAPI because old SAR writes `MSEP`; signed binaries/INF are never patched. See [Microsoft's guidance](https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/implementing-audio-processing-objects).

The 2018 cross-signed driver loaded on Windows 11 build 26300, where Secure Boot and Memory Integrity were **already off**. Compatibility with stronger security policies is unverified. The installer never changes security/boot/signature policy. Driver rejection rolls back the new owned device and Rey payload/service. Uninstall removes the Rey root, preserving the shared driver-store package and card settings.

See [qualification](WINDOWS-PAIRS-2026-10-17.md) and [evidence](evidence/rey-windows-pairs-20261017.json). Synthetic, real USB/JACK and physical ADC/DAC evidence are kept separate. Long DAW load, exclusive WASAPI, sleep/resume and physical hotplug remain unqualified. This is a development candidate.

Unsigned Rey ACX and the earlier TAG prototype remain research and are not installed. The earlier Matrix trial was removed; Matrix is not a dependency or autostart component.
