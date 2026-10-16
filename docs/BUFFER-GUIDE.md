# ASIO buffer and automatic transport policy

The user selects one ASIO buffer: **16, 32, 64, 128 or 256 frames**. The DAW can
choose it when opening the driver. The Pi uses the same JACK period except that
16 ASIO frames use a 32-frame JACK period. Begin with 64 or 128, then check the
actual project and its drop/late/overflow counters.

USB requests and render reserve are internal, fixed per-rate policies:

| Rate, kHz | Outstanding requests | Host render reserve, µs |
|---|---:|---:|
| 44.1 | 8 | 1000 |
| 48 | 8 | 1000 |
| 88.2 | 8 | 750 |
| 96 | 8 | 750 |
| 176.4 | 8 | 1500 |
| 192 | 8 | 1500 |

The lead is `ceil(ceil(rate × reserve_us / 1e6) / block) + 1` blocks. The class
UAC2 compatibility path uses its own actual Windows endpoint buffers. Legacy
`RenderLeadBlocks`, queue and guard preferences do not override the current
production policy. There are no packet/queue controls in the panel.

Eight requests cover bus scheduling variation; their capacity is not itself an
analog latency figure. The shared rings have 64 slots to absorb a Windows batch
without forcing a matching extra render delay. The ASIO thread has a preferred
physical core, with normal scheduler migration retained instead of a one-core
CPU set. Caller affinity and diagnostic overrides remain respected.

The Pi reconciles its AKM clock with the USB stream using an allocation-free
JACK callback and preconfigured resamplers. Its FIFO reserve and rate-specific
filters also contribute latency. ASIO `getLatencies` and the panel's buffer
calculation currently describe the PC queues, not the entire Pi/converter path.
Measure an actual loop for full timing; do not use those numbers as physical RTT.

A good median with missing data is not a stable result. Check both PC counters
and the Pi's native receive/send underflow/overflow, conversion and xrun counts.
The current digital qualification is separate from the retained
[2.8.1 fixture results](USB-LATENCY-2.8.1.md). Increasing the user buffer can help
heavier projects; no short test proves every project or Windows workload.
