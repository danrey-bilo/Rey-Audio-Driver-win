**English** | [Русский](LAN-LATENCY-2026-09-29.ru.md)

# Historical Windows receive-gap investigation — 2026-09-29

This report records a specific Pi 5 / Windows 11 / Intel I225-V test system and older diagnostic builds. It is not a latency claim for version 2.4.3. The source/sink were synthetic: 8×8, 192 kHz PCM32, 16-frame capture packets and ASIO64. The detailed chronological data remain in the Russian edition.

| ASIO/LAN, duration | Observation | Maximum receive gap |
|---|---|---:|
| 64/128, 180 s | Missing/late frames and deadline misses in all tested variants | about 1.2–1.7 ms |
| 64/256, 180 s | Some runs had missing frames or a resync with skipped frames | about 1.65–1.72 ms |
| 64/320, 180 s | One clean experimental run | 1.636 ms |
| 64/320, 600 s | 16 missing/late frames | 1.897 ms |
| 64/384, 180 s | No missing/late/resync/deadline/skipped errors | 1.277 ms |
| 64/384, installed DLL, 600 s | Same counters and TX errors zero | 1.779 ms |

A sample of 2,160,363 receive intervals had p50 below 85 µs, p95 below 100 µs and p99 below 125 µs, yet 13 intervals exceeded 1 ms. Packets arrived in batches after these pauses. A `recv` interval is not cable one-way latency.

The usable continuous portion of an Intel PktMon capture contained 972,009 consecutive packets over the last 81 seconds. Capture-point intervals were p50 84 µs, p95 94 µs, p99 117 µs and max 1352 µs. Software capture points are not hardware PHY timestamps; NIC, IRQ/DPC, NDIS and capture overhead were not fully separated by that trace. PktMon itself increased stream errors in the test.

Larger capture packets, socket busy polling and a Windows high-performance power plan did not fix the low-buffer case. Polling used about a CPU core and still lost frames. Experimental delayed queue reset was removed because it did not solve the underlying receive pauses. These are historical experiments, not recommended global settings.

The clean 600-second 64/384 result established only that digital test window. Real DAW workload and physical ADC/DAC qualification remained separate. Current users should select buffers manually using the [buffer guide](BUFFER-GUIDE.md).
