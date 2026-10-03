**English** | [Русский](VALIDATION.ru.md)

# Validation 2.5.0

| Check | Result / scope |
|---|---|
| Windows x64 Release | 13/13 CTest: core, transport, datagrams, leases, budget, PCM, examples, config, IOCP, bridge, session, StartGate |
| Standalone Windows service | ASIO disabled, no ASIO SDK configured; service/probe build passed |
| Pi5 ARM64 Release | 9/9 native CTest with RT policy; GCC14, Debian13, PREEMPT_RT 6.18.50 |
| Installed SDK exports | Three consumers passed: Windows AoIP, ARM64 AoIP, ARM64 Pi5; exact 2.5.0 |
| ACX x64 | Clang22 MSVC ABI build, InfVerif and Inf2Cat passed; unsigned/uninstalled |
| Packaging | MSI tables/payload/versions and ARM64 DEB metadata/content inspected; no new installation lifecycle |

Fresh binaries, 300-second block256/guard1536/callback50 window: **PASS**, digital RTT p50/p95/p99/max **8.920/9.446/9.573/16.599 ms**, 1,123,192 samples. Primary Windows errors: zero.

Windows service SHA-256: `9b80dc78adc192cba79acbdfd6bb4a6ef7ba341cfe5862197782829bd1b38f63`. Pi5 SHA-256: `252a146bad247a75332b6710a52b04bd0a7360c3bae1fd47163d3e380a2aad1c`.

Callback/wake/RX gap/TX age max: 161.6/692.9/7270.0/884.0 us. Pi process CPU: 7.935% of one core. Full Windows worker CPU and all error/interface/throttle counters are in the linked JSON. Before/after throttle snapshots do not monitor power continuously.

The original installed Pi binary/configuration, LAN/Wi-Fi and CPU isolation were restored and independently checked. The runtime DEB peer hash matches this native candidate.

The earlier fresh-build block256/guard1024 case ran for 900 s and **FAILED**: late/missing=1568/1568, RX gap max=7427.9 us. Its low median and a prior clean window do not establish a reliable minimum. The new larger-guard case is a separate manual profile, with the unchanged wire budget. All subsequent cases are limited to 300 seconds by the project owner.

These are synthetic digital Pi → Windows service echo → Pi measurements. ADC/DAC, a real DAW, installed ACX/WASAPI, supported MSVC driver build and guard1024 fault recovery remain unqualified. Buffers are manual. Multiple Pi devices are deferred. USB will have a separate repository/library; its implementation is on hold. Pi4 was not rebuilt or retested and remains unchanged. **Pre-release.**

[Complete measurements and failed cases](https://github.com/danrey-bilo/Win11-asio-AoIP/blob/v2.5.0/docs/measurements/README.md) · [Release notes](RELEASE-2.5.0.md)
