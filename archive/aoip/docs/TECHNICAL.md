**English** | [Русский](TECHNICAL.ru.md)

# Transport limits and latency

The audio path is PCM → PiAoIP → UDP → IPv4 → Ethernet. It uses ordinary OS sockets. The current release does not implement AES67/Dante, PTP, hardware timestamp synchronization, FEC or retransmission.

| Parameter | Supported range |
|---|---|
| Physical channels | Up to 64 per direction; actual counts come from the device |
| Rates | 44.1, 48, 88.2, 96, 176.4, 192 kHz |
| PCM | Signed integer 16, packed 24, 32 bit |
| ASIO block | 16–2048 samples, powers of two |
| UDP payload | At most 1472 bytes with MTU 1500 |
| Session | One independent Pi/PC pair |

PCM bandwidth is `channels × rate × bits` per active direction, plus packet/network overhead. At 8×192 kHz/PCM32 it is 49.152 Mbit/s per direction. Small packets increase packet rate and scheduling work. A supported profile is not a guarantee of sub-millisecond latency.

Buffer duration is `samples × 1000 / rate`. ASIO buffering and the manual LAN buffer are separate parts of the path. Do not sum displayed numbers and describe the result as a measured ADC-to-DAC delay. Measure digital round trip with a known route, and measure physical converters separately when a hardware backend exists.

Record run duration, exact profile, p50/p95/p99/max, missing/late frames, deadline misses, RX gaps, queue overflows, expired TX, TX errors and CPU use. A zero-error short run or a low median alone does not establish long-term reliability.

The supplied Raspberry Pi service generates synthetic PCM and checks the returned stream. Physical I2S/USB/ADC/DAC integration and effects processing are outside this release.
