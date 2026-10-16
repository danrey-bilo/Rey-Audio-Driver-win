# Rey AUSB native transport

Rey Audio USB ASIO uses the application's own **AUSB wire protocol** through
WinUSB and the Raspberry Pi FunctionFS/native-AIO implementation. There is no
UAC2 or WASAPI fallback in the Windows service. The Pi configuration exposes
only vendor audio and a separate CDC management function; CDC carries no PCM.

Eight capture and eight render channels use PCM16/24/32 at 44.1, 48, 88.2, 96,
176.4 or 192 kHz. Effects modes stop at 96 kHz; higher rates are soundcard-only.
The high-speed interrupt endpoints have `bInterval=1` and 832-byte capacity.
The individual packet interval is **125 microseconds**. Two writes of four
render packets reduce completion overhead while preserving that interval.
384 kHz is not advertised.

Backend 3 follows the physical AKM/JACK clock through packet frame counts.
Completed capture is published to ASIO before collecting render, so waiting
for the host cannot withhold capture that the host needs. Render waits are
bounded, frame/sequence/epoch validation is strict, and shortages are counted.
Queue depth and reserve are internal per-rate policies. The ASIO buffer remains
user-selectable. Integer/float conversion remains; bit-exact PCM32 is not claimed.

This is a custom user-space transport using Microsoft's existing WinUSB driver,
not a newly implemented Windows kernel USB driver. Ordinary Windows system audio
without ASIO needs a separate native endpoint driver and is not supplied here.

Install matching Windows and Pi packages together. The board DeviceId and saved
routes remain stable, although Windows can assign the CDC function a new COM
number. The panel discovers the current port. The Pi updater backs up the owned
gadget/status scripts, service unit and audio binaries, with timed rollback.

See the [qualification report](USB-LATENCY-2026-10-16.md) for measured latency,
buffers, counters and exact source revisions. A digital USB/JACK loop is not an
analog ADC/DAC measurement or a real DAW session qualification.
