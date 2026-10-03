# Manual USB-ASIO buffers

ASIO block: 16, 32, 64, 128, 256 frames. Render lead: 1–4 blocks. USB depth:
1–16 packets. Controls are independent and manual; there is no automatic
minimum-buffer selection.

The 2.8.1 control profiles for 96/192 kHz are documented in the
[low-latency report](USB-LATENCY-2.8.1.md). The Pi needs the updated USB runtime
with FIFO 40 and locked memory; a Windows buffer change alone is insufficient.

Buffer-model examples at USB depth4:

| Rate / block / lead | Reported total |
|---|---|
| 192 kHz / 64 / 3 | 1.5052 ms |
| 192 kHz / 64 / 4 | 1.8385 ms |
| 96 kHz / 32 / 3 | 1.5104 ms |
| 96 kHz / 32 / 4 | 1.8438 ms |

These are ASIO getLatencies values, not measured analog round trip. A smaller
block leaves less host scheduling time. The same block takes longer at lower
rates. Earlier short 16/32 successes did not prove sustained stability; longer
runs on the earlier build had errors. [2.8.0 history](USB-ONLY-2.8.md) is retained
separately from the new measurements.

Settings → ASIO → Save, then reopen the driver in Ableton. Apply USB rate/bits/depth
separately after stopping ASIO. Test the intended project, drops/late/overflow,
frame continuity and RTT; use 175 seconds, finishing within three minutes. A good median with
missing frames is not stable. Check generation and connection_id when comparing
counters. Earlier depth2 slowed the digital bench despite zero PCM errors, so
zero counters alone are insufficient.

ASIO reserve and USB queue are under **Advanced** on their respective cards.
