**English** · [Русский](USB-ASIO.ru.md)

# Rey Audio USB ASIO — AKM development candidate

One Raspberry Pi 5 board, eight inputs and eight outputs, PCM16/24/32.
Supported rates: **44.1 / 48 / 88.2 / 96 / 176.4 / 192 kHz**. 384 kHz is not advertised.

Install the matching **Rey-Audio-USB-ASIO-Setup-x64.exe** and accept Windows UAC.
The package contains the ASIO DLL, automatic service and desktop/tray panel.
Close the DAW before upgrading a loaded ASIO DLL. All four payloads must come
from the same build. The installer verifies their hashes and preserves preferences.

The Pi must run the coordinated Rey Effects native USB integration. The pinned
Pi5-AUSB 0.2 dependency supplies WinUSB/FunctionFS, not an analog converter driver.
Ordinary Windows playback/recording uses the composite device's separate UAC2
function. ASIO uses the native WinUSB interface and takes priority while active.
The driver does not require a custom Windows SYS file or Test Mode.

## Controls

**Device** contains two PC modes: **Audio interface** and **Combined**. Combined
supports 44.1–96 kHz. Higher rates require Audio interface, which disables effects.
On unplug the Pi selects its saved standalone effects profile; reconnect restores
the last PC mode and per-channel routes. USB owns the profile while connected.

The audio format has three fields: **Rate**, **Buffer**, **PCM bits**. USB request
count and render reserve are fixed internal policies for each rate. Legacy manual
queue/reserve values are ignored. The DAW may choose its ASIO buffer; 16 ASIO
frames use a 32-frame JACK period. Start with 64 or 128 and qualify the actual project.

For each channel, the optional route disclosure selects capture before/after
effects and playback directly to the DAC/through effects. There is no mode
selector in the Pi web interface.

1. Stop the DAW audio engine before changing rate, bits or board mode.
2. Apply the format in Rey; choose **Rey Audio USB ASIO** in the DAW.
3. Reopen ASIO after a USB disconnect/reappearance or driver update.

The tray panel starts hidden at login and its icon remains available even if USB
is absent. Windows may place it in the notification-area overflow. Audio status
changes the icon state; it does not control the panel's lifetime.

## Timing and validation

The Diagnostics page reports ASIO queue/callback counters. The displayed ASIO
buffer calculation excludes Pi/JACK/clock-conversion and ADC/DAC latency; it is
not measured round-trip latency. The real digital USB/JACK loop is measured
separately. A 125 microsecond USB interval is not the complete audio latency.

See the current coordinated qualification report and retained
[2.8.1 measurements](USB-LATENCY-2.8.1.md) for the earlier digital fixture. These
are development candidates; successful transport tests do not qualify an analog
loop or every DAW project.

## Build

Initialize the pinned transport with `git submodule update --init --recursive`.
An external Steinberg ASIO SDK is required and is never bundled. A build with
`REY_FINITE_QUALIFICATION=ON` uses an isolated pipe, is console-only and cannot
register its DLL or be packaged by the installer tool. It is a test artifact.
