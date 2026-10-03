**English** | [Русский](GATEWAY.ru.md)

# Signed Windows endpoints: local TAG prototype

These results belong to the earlier 2.7 local evaluation binaries. The USB-only
2.8 installer does not include TAG or create Windows endpoints. This optional
USB backend remains experimental; its old measurements do not qualify 2.8.

The optional Rey service backend uses the **original Thin Audio Gateway demo
2.0.0.1903** to provide Windows audio input and output without loading Rey ACX.
On 2026-10-03, the original SYS/CAT signatures were valid, the driver was running
and its PnP device had status OK. The signer was Microsoft Windows Hardware
Compatibility Publisher. Test Mode was off; VBS and Memory Integrity remained
active. No boot settings were changed for this test.

This is a working local prototype for **one real Pi5 and one multichannel
capture/render pair**. It is outside the published 2.7.0 ACX MSI/EXE.
The vendor supplies this package as a demo; a permanently free production
dependency has not been established. Distribution questions are deferred.

## Two audio paths

| Path | Intended behavior | Current evidence |
|---|---|---|
| Windows applications | Windows audio API → signed virtual endpoints → Rey mixer/service → USB | Earlier real-Pi USB and WASAPI evaluation; the shared engine used a 10 ms period |
| ASIO/Ableton | ASIO frontend → bounded shared-memory exchange with Rey service → USB | [USB-ASIO preview](USB-ASIO.md), separate from TAG |

**Less than 2 ms applies to audio roundtrip in ASIO/Ableton.** It does not apply
to Chrome, Telegram or the Windows shared-audio engine. ASIO must bypass the
TAG/WASAPI path. The USB transport's microframe interval and its earlier digital
RTT results do not measure complete Ableton or physical ADC/DAC latency.

The service remains the single transport owner so Windows and ASIO can share
one Pi. The USB ASIO frontend implements bounded PCM rings, stream position,
generation validation and callback/deadline counters. Independent device-clock
drift handling and simultaneous Windows/ASIO audio still need qualification.

## Structure

| File | Responsibility |
|---|---|
| `src/bridge/gateway_abi.hpp` | Fixed TAG 2.0 C API descriptors and checked x64 sizes/offsets |
| `src/bridge/gateway_bridge.*` | SDK ownership, endpoint format/state, shared PCM buffers and cleanup |
| `src/bridge/driver_bridge.*` | Select the optional TAG backend or the existing ACX bridge |
| `tools/gateway/loopback_probe.cpp` | Exclusive Windows playback/capture with eight distinct PCM markers |
| `tools/gateway/verify_formats.py` | Finite, isolated real-Pi format matrix; ≤120 seconds |

Build with `REY_ENABLE_TAG_BRIDGE=ON`; its default is **OFF**. The adapter loads
`tagapi.dll` only from the service executable's directory, with DLL-directory
and Windows-system dependency search. The original vendor driver and SDK DLL
are external dependencies. Their binaries and certificate chain are not edited.

The transport callback reads the driver's render ring and writes its capture
ring directly. It owns the sample clock; no SDK polling thread or extra PCM
queue is added. PCM16, packed PCM24 and PCM32 retain their native container
sizes. The callback allocates no memory. It validates format, buffer capacity
and frame bounds, acknowledges stream transitions and advances device position.
SDK notification calls still cross into the kernel and their time is measured
as part of callback processing.

The demo's existing lines are used as **TAG Speakers** and **TAG Microphone**.
When the service stops, the endpoints become unplugged. This prototype does
not create the four stereo pairs. An attempted additional render line returned `0x80070044`; the temporary
capture line was removed. Additional topology remains unqualified.

## Results

[Compact machine-readable evidence](evidence/rey-tag-local-20261003.json).
All runs used the Pi's **digital-loopback** backend. No ADC/DAC was connected
or measured. Production service callbacks were used, with `digital_test=false`.

| Check | Measured result |
|---|---|
| Native contracts after the final code change | 24/24 passed |
| Windows exclusive format acceptance | 36/36: two directions × six rates × three bit depths, 8 channels |
| Real Windows → TAG → Rey → USB → Pi → return marker checks | 18/18 PCM profiles; eight correct channels; no marker mismatches or capture discontinuities |
| Profiles | 44.1/48/88.2/96/176.4/192 kHz × PCM16/24/32; about 2 seconds per profile |
| Dedicated 192 kHz/PCM32 exclusive check | 380,615 nonzero frames matched per channel, zero wrong values, zero discontinuities; 3 ms Windows exclusive period |
| USB/service continuity run | 300.110 s wall time; 299.823 s USB segment; 2,393,112 callbacks / 57,434,688 frames |
| USB counters in that run | Missing frames, layout underrun/overrun and USB errors: zero; generation stayed 1 |
| Longest callback/receive gap | Approximately 1,957 µs; gap percentiles were not collected |
| Service processing at the 295 s snapshot | Mean 2.361 µs; maximum 273.6 µs |
| Service CPU at the 295 s snapshot | 12.531 CPU seconds, 4.247% of one logical core |
| Concurrent shared WASAPI capture, 30.007 s | 5,748,480 frames; one discontinuity; zero timestamp errors |
| Concurrent shared WASAPI render, 30.001 s | 5,748,480 frames; zero discontinuities/timestamp errors |

Shared capture notification gaps p50/p95/p99/max were
10,001.2 / 10,243.0 / 10,353.2 / 12,150.0 µs; render gaps were
10,001.2 / 10,240.7 / 10,349.5 / 12,127.6 µs. These describe Windows
notifications, **not ASIO or roundtrip latency**. The capture discontinuity
was reported during the run; its exact cause was not traced.

The original shared-mode marker comparison failed in the first two channels;
six channels matched. The complete 300-second scenario therefore remains
marked **false** in its raw report, even though its continuity counters passed.
The subsequent exclusive PCM checks matched all eight channels in every profile.
This isolates the difference to the shared session's behavior; it does not
identify which application, volume stage or audio processing caused it.

The first matrix attempt also reached a capture endpoint while Windows was
reopening it after a profile change (`AUDCLNT_E_DEVICE_INVALIDATED`). The
reproducible harness allows up to five startup attempts for this exact activation
condition and never retries PCM mismatches or running-stream failures. The
final 18-profile run used one startup attempt per case. Format changes restart
the affected USB session; opening/closing a client within a profile did not.

The continuity run used service SHA256
`8873305c92f0582cb0f8783cede9ebceb8076eaaba09ef9a5707362a700b85c3`.
After stronger format/error checks, the matrix used
`6cd319d769269cdbe05766342174da54a1b1a6f28672a92232bfe03099ec8caf`.
These are local evaluation binaries, not new release assets.

## Reproduce

Install the original vendor demo through its supplied installer/driver manager.
Build Rey locally with `REY_ENABLE_TAG_BRIDGE=ON`, and place the original x64
`tagapi.dll` beside `ReyAudioService.exe`. Build the marker probe with a Windows
C++17 compiler and link `ole32`, `uuid` and `avrt`. With MinGW use `-municode`.
The optional minimal-toolchain `REY_WASAPI_HEADERS` directory supplies
WASAPI and `mmreg.h` headers where needed.

Obtain the TAG endpoint IDs from `ReyAudioProbe`, then run:

```powershell
python tools/gateway/verify_formats.py --service build/gateway/bin/ReyAudioService.exe `
  --probe build/gateway/bin/GatewayLoopbackProbe.exe --out build/gateway/proof `
  --render-id '<TAG render endpoint ID>' --capture-id '<TAG capture endpoint ID>'
```

This starts a finite console service with a new isolated `.dat` test profile.
It does not install a service, modify normal card settings or change boot/security
settings. The `.dat` is test storage, not a user-editable installation INI.
Stop any existing Rey service/host before testing exclusive ownership of TAG.
The current local USB-ASIO installer is independent of this endpoint prototype.

Remaining qualification: simultaneous Windows/USB-ASIO and Ableton ≤300-second
roundtrip/deadline tests, independent hardware audio clock and ADC/DAC,
Chrome/Telegram sessions, installation lifecycle and additional endpoint topology.
Pi4, Pi firmware and USB wire protocol were unchanged in this work.

[Original TAG product and demo](https://software.muzychenko.net/en/thin-audio-gateway/)
· [Normal Windows options](INSTALL-NORMAL-WINDOWS.md) · [Mixer](MIXER.md)
