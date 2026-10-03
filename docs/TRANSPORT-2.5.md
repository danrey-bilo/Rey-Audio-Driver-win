**English** | [Русский](TRANSPORT-2.5.ru.md)

# Single-Pi transport, Windows service and development driver

PiAoIP 2.5.0 reduces memory work, controls callback receive work and adds an independent Windows service. The existing ASIO adapter remains available. A development ACX/KMDF driver provides the basis for Windows capture/render endpoints. The current hardware validation covers one Raspberry Pi 5 and the built-in Gigabit Ethernet ports.

## Implementation

| Component | Change and operating rule |
|---|---|
| Packet queues | Reserve/commit and acquire/release slots avoid whole-Packet copies. One producer and one consumer own their respective slots; reset only after both stop. Full queues remain observable. |
| Receive work | A cooperative per-pass budget is checked between packets: min(period/4, 50 us). Expired frames are discarded with frame indices preserved. This is not an OS scheduling guarantee. |
| Windows receive | A single-session IOCP receiver uses overlapped buffers, bounded completions and explicit cancellation. A common dispatcher across several Pi devices is deferred. |
| PCM24 | x86 SSSE3 dispatch with scalar fallback; Pi ARM64 uses the measured NEON row/scalar bulk path. PCM16/24/32 conversion preserves signed integer samples and checks scalar tails. |
| Linux datagrams | Optional recvmmsg/sendmmsg consumes already-ready packets without waiting for a batch to fill. The confirmed single-Pi profile uses socket batch=1. |
| Wire accounting | Per-direction PCM, audio/UDP/IP/Ethernet overhead and packet rate are reported separately. VLAN/tunnels require their own overhead model. |
| Identity/session | Board serial is preferred; machine-id fallback requires unique provisioning. V3 leases reject a second owner with BUSY. DeviceId is not authentication. |
| Worker startup | StartGate prepares MMCSS/timers and workers before PCM subscription, publishes the acknowledged epoch, then releases workers. Stop also wakes an unreleased session. |
| PCM bridge | A versioned, bounded service/driver contract validates channel counts, buffers, frame positions and stream ownership. The development METHOD_BUFFERED path adds copies/syscalls. |

The transport core remains C++17. The Windows service builds with `PIAOIP_BUILD_ASIO=OFF`; the ASIO SDK is required only for the legacy adapter and its diagnostic hosts. IEEE CRC is preserved: SSE4.2 CRC32C is not a compatible substitute.

## Confirmed development profile

The profile search used 15 short 30-second windows and three separate 900-second windows. The binaries used for that search are identified in its measurement record; release rebuild checks are recorded separately.

| Block / guard, callback 50 us | Time | Digital RTT p50 / p95 / p99 / max, ms | Errors | Result |
|---|---:|---|---|---|
| 128 / 448 | 900 s | 2.908 / 3.145 / 3.221 / 8.075 | late/missing=5312; host_overruns=1 | FAIL |
| 256 / 1536 | 900 s | 8.918 / 9.419 / 9.553 / 13.532 | Zero error counters | PASS |
| 256 / 1024 | 900 s | 6.251 / 6.755 / 6.891 / 11.718 | Zero error counters | PASS |

The fastest profile confirmed in that search is **8 inputs + 8 outputs, 192 kHz, PCM24, capture32, block256, guard1024**, continuous PCM, EnergySaving=0. Guard is 5.333 ms and callback period is 1.333 ms. The final window has 3,373,249 RTT samples. Callback/wake/RX gap/TX age maxima are 118.2/1026.0/5839.4/642.8 us. Windows audio/RX/TX CPU is 7.212/3.017/1.929% of one logical processor; Pi process CPU is 7.986% of one core.

Missing/late, deadline/skipped, resync, host overruns, expired output, queue/socket/control/MMCSS/IO errors, Pi loss/error counters and eth0 error/drop deltas are zero in the confirmed window. Reordering is reported separately. Current throttle flags were clear at both boundary snapshots; historical 0x50000 persisted. Power was not monitored continuously.

| Direction | Calculated wire Mbps | Packets/s |
|---|---:|---:|
| Capture Pi → Windows | 42.336 | 6000 |
| Render Windows → Pi | 40.284 | 3750 |

Reducing only guard1536 to guard1024 reduced measured median RTT by 2.667 ms without changing the wire budget. Smaller guards512/768 at block256 passed only short screens; their 15-minute stability is not confirmed. RX gap is an observation interval, not the age of every packet before its presentation deadline. Frame indices, callback phase and actual counters determine the outcome.

These are digital Pi → Windows service echo → Pi measurements. Do not divide RTT by two to claim one-way capture/render latency. ADC/DAC, installed ACX/WASAPI and a real DAW were not measured. Manual buffers stay manual. New LAN cases are capped at five minutes (300 seconds); the earlier 900-second records retain their original duration. Windows NIC/network priority was not retuned.

## Windows endpoints and the ASIO transition

The independent service owns UDP, clock/routing, the Pi identity and session. The kernel driver owns audio streams, WaveRT packets, presentation positions and bounded PCM exchange. Network calls stay outside the audio kernel callback.

The current service supports one Pi with **one multichannel capture endpoint and one multichannel render endpoint**, up to eight channels each. A dynamic child is tied to DeviceId; reconnect/profile changes retire the old child/session. This is not independent mono/stereo endpoints per channel or a qualified multi-Pi aggregate.

The x64 driver is an unsigned development build. INF/catalog checks and compilation do not prove installation, PnP, WASAPI or kernel runtime correctness. The ASIO MSI does not install ACX. Signing, supported MSVC/WDK build, endpoint enumeration, shared/raw/exclusive streams, positions, duplex, Driver Verifier, crash/removal/sleep and DAW/hardware loopback remain qualification work.

## USB and deferred work

USB is reserved for a separate repository and transport library. Development is on hold at the project owner’s request; this release has no USB audio interface. LAN remains the default when both paths identify the same DeviceId; switching will be explicit and session ownership shared. High-speed USB microframes are 125 us, while the reviewed WinUSB isochronous completion grouping has a 1 ms constraint. Bulk, vendor interrupt and isochronous paths need separate measured prototypes; 125 us bus intervals do not establish application audio latency. No USB gadget or USB PCM path is enabled by this release.

Several Pi devices, aggregate clocks/ASRC, RIO A/B, accelerated compatible IEEE CRC, kernel WSK and NDIS/L2 experiments are deferred. Pi4 source, dependency pins, packages and releases remain at their prior version because this series did not test Pi4.

## Verification and use

Use the matching 2.5.0 components. Existing installed device/Windows profiles are preserved; explicitly select a new test profile. The Pi owns rate/bit depth: Windows service INI Rate/Bits do not reconfigure it. Fresh Pi5 installations start at 8×8 / 192 kHz / PCM24 / capture32.

See [build instructions](BUILD.md), [validation](VALIDATION.md) and [release notes](RELEASE-2.5.0.md). The original Pi executable/configuration, LAN/Wi-Fi and CPU isolation were restored and independently checked after the profile search. Earlier FAIL results, including guard512 PCM recovery after a link cycle, remain part of the evidence.


[Measurement records, failed cases and binary hashes](https://github.com/danrey-bilo/Win11-asio-AoIP/blob/v2.5.0/docs/measurements/README.md)


## Release rebuild result

Fresh binaries, 300-second block256/guard1536/callback50 window: **PASS**, digital RTT p50/p95/p99/max **8.920/9.446/9.573/16.599 ms**, 1,123,192 samples. Primary Windows errors: zero.

Windows service SHA-256: `9b80dc78adc192cba79acbdfd6bb4a6ef7ba341cfe5862197782829bd1b38f63`. Pi5 SHA-256: `252a146bad247a75332b6710a52b04bd0a7360c3bae1fd47163d3e380a2aad1c`.

Callback/wake/RX gap/TX age max: 161.6/692.9/7270.0/884.0 us. Pi process CPU: 7.935% of one core. Full Windows worker CPU and all error/interface/throttle counters are in the linked JSON. Before/after throttle snapshots do not monitor power continuously.

The original installed Pi binary/configuration, LAN/Wi-Fi and CPU isolation were restored and independently checked. The runtime DEB peer hash matches this native candidate.

The earlier fresh-build block256/guard1024 case ran for 900 s and **FAILED**: late/missing=1568/1568, RX gap max=7427.9 us. Its low median and a prior clean window do not establish a reliable minimum. The new larger-guard case is a separate manual profile, with the unchanged wire budget. All subsequent cases are limited to 300 seconds by the project owner.

Manual release starting profile: block256/guard1536, confirmed for 300 seconds. This is not a universal stability guarantee.
