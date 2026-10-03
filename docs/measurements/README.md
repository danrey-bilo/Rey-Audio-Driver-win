**English** | [Русский](README.ru.md)

# LAN measurements and release checks

Fresh binaries, 300-second block256/guard1536/callback50 window: **PASS**, digital RTT p50/p95/p99/max **8.920/9.446/9.573/16.599 ms**, 1,123,192 samples. Primary Windows errors: zero.

Windows service SHA-256: `9b80dc78adc192cba79acbdfd6bb4a6ef7ba341cfe5862197782829bd1b38f63`. Pi5 SHA-256: `252a146bad247a75332b6710a52b04bd0a7360c3bae1fd47163d3e380a2aad1c`.

Callback/wake/RX gap/TX age max: 161.6/692.9/7270.0/884.0 us. Pi process CPU: 7.935% of one core. Full Windows worker CPU and all error/interface/throttle counters are in the linked JSON. Before/after throttle snapshots do not monitor power continuously.

The original installed Pi binary/configuration, LAN/Wi-Fi and CPU isolation were restored and independently checked. The runtime DEB peer hash matches this native candidate.

The earlier fresh-build block256/guard1024 case ran for 900 s and **FAILED**: late/missing=1568/1568, RX gap max=7427.9 us. Its low median and a prior clean window do not establish a reliable minimum. The new larger-guard case is a separate manual profile, with the unchanged wire budget. All subsequent cases are limited to 300 seconds by the project owner.

[Fresh release JSON](evidence/release-2.5.0-lan-margin-5min/release-b256-g1536-cb50.json) · [Restoration](evidence/release-2.5.0-lan-margin-5min/restoration-verified.json)

The earlier minimum search used different binary hashes: block256/guard1024 passed for 900 s at 6.251/6.755/6.891/11.718 ms. The 128/448 candidate failed with 5312 late/missing frames and one host overrun. Short clean screens do not qualify smaller guards. All failed and successful finite suites are preserved below.

- [AOIP_LAN_15MIN_VALIDATION_2026-10-03_RU](AOIP_LAN_15MIN_VALIDATION_2026-10-03_RU.md)
- [AOIP_LAN_MARGIN_VALIDATION_2026-10-03_RU](AOIP_LAN_MARGIN_VALIDATION_2026-10-03_RU.md)
- [AOIP_LAN_MINIMUM_LATENCY_2026-10-03_RU](AOIP_LAN_MINIMUM_LATENCY_2026-10-03_RU.md)
- [AOIP_LAN_STARTUP_FIX_2026-10-03_RU](AOIP_LAN_STARTUP_FIX_2026-10-03_RU.md)

These are synthetic digital Pi → Windows service echo → Pi measurements. ADC/DAC, a real DAW, installed ACX/WASAPI, supported MSVC driver build and guard1024 fault recovery remain unqualified. Buffers are manual. Multiple Pi devices are deferred. USB will have a separate repository/library; its implementation is on hold. Pi4 was not rebuilt or retested and remains unchanged. **Pre-release.**

Portable export only normalizes absolute workspace paths. Numeric measurements and binary hashes are unchanged; [export provenance](export-manifest.json) records original and exported hashes. No raw local evidence was overwritten.
