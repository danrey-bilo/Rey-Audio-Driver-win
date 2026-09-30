**English** | [Русский](BUFFER-GUIDE.ru.md)

# Choose buffers manually

The main page contains **ASIO buffer** and **LAN buffer** in samples. The duration under each field is calculated for the selected rate. At 192 kHz, 64 samples are 0.333 ms and 384 samples are 2.000 ms. These numbers describe buffers, not a measured round trip.

1. Set the required rate, bit depth and enabled channels.
2. Start with conservative ASIO and LAN buffers and a representative DAW project.
3. Keep ASIO fixed while reducing LAN in small steps. Watch missing/late frames, RX gaps, queue overflows and TX/deadline errors.
4. Once LAN is reliable for the test workload, keep it fixed and reduce ASIO. Check callback/host overruns as well as network counters.
5. Restore the last clean setting if errors grow. Recheck under your heaviest normal workload for a meaningful duration.

There is no automatic tuning mode. A clean five-minute run is evidence for that run, not a guarantee for every project or computer. Keep the profile, duration, p50/p95/p99/max digital RTT and error counters together. Driver defaults are generic; no NIC-specific power, IRQ or global Windows scheduler tweaks are applied.

Use **Diagnostics** from the DAW-hosted panel for that stream's counters. The separate tray panel cannot read counters from another ASIO process. Physical ADC/DAC latency remains unmeasured until an actual hardware audio backend and loopback are connected.
