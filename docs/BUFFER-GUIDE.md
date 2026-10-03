# Manual USB-ASIO buffers

ASIO block: 16, 32, 64, 128, 256 frames. Render lead: 1–4 blocks. USB depth:
1–16 packets. Controls are independent and manual; there is no automatic
minimum-buffer selection.

At 192000 Hz, USB depth3:

| Block / lead | Reported total |
|---|---|
| 64 / 3 | 1.3802 ms |
| 64 / 4 | 1.7135 ms |
| 32 / 3 | 0.8802 ms |
| 16 / 4 | 0.7135 ms |

These are ASIO getLatencies values, not measured analog round trip. A smaller
block leaves less host scheduling time. The same block takes longer at lower
rates. Earlier short 16/32 successes did not prove sustained stability; longer
runs had errors. [Exact 2.8 build results](USB-ONLY-2.8.md).

USB → Save ASIO, then reopen the driver in Ableton. Apply USB rate/bits/depth
separately after stopping ASIO. Test the intended project, drops/late/overflow,
frame continuity and RTT; each run is at most 295 seconds. A good median with
missing frames is not stable. Check generation and connection_id when comparing
counters. Earlier depth2 slowed the digital bench despite zero PCM errors, so
zero counters alone are insufficient.
