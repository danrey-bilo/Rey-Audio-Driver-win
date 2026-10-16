**English** · [Русский](BUFFER-GUIDE.ru.md)

# ASIO buffer and automatic USB policy

Choose the **ASIO buffer** in the DAW or Rey panel. The driver sets USB queue
depth and render reserve internally for the selected rate; old manual queue/
lead settings are ignored. 384 kHz is not advertised.

ASIO16 uses a 32-frame JACK period and is the minimum-latency qualification
profile. Larger buffers remain selectable for heavier sessions. The measured
[USB/JACK report](USB-LATENCY-2026-10-16.md) identifies the exact buffer and test
duration; no buffer choice guarantees every DAW workload.

JACK periods 128/256 use a separate receive reserve covering the measured host
burst/gap. That improves their margin and adds latency; it does not change JACK32.

The high-speed USB interval is 125 microseconds. Eight capture requests and
two render requests of four packets keep transfers in flight. A grouped write
does not turn the individual packet interval into 500 microseconds or 1 ms.
The internal render reserve is 333 microseconds, or 500 at 192 kHz, rounded to
ASIO blocks. At 192 kHz this covers the complete render burst before waiting.

Native ASIO follows the AKM/JACK clock by returning packet frame counts; it
does not resample PCM in time. The Pi FIFO policy and physical I/O still add
latency. The service has no UAC2 or WASAPI fallback.
The float engine is not bit-exact PCM32.

On unrestricted PCs with enough fast physical cores, service and ASIO callback
threads use separate two-core CPU Sets. They can migrate within those pools.
Existing process/thread CPU restrictions and explicit diagnostic overrides are
preserved; no machine-wide processor or power setting is changed.
During streaming, completion polling spins for at most 75 microseconds before
waiting on the USB event. This trades some single-core CPU time for shorter
scheduler sleeps; the qualification report records the measured CPU cost.

ASIO getLatencies and the panel describe PC buffers. They do not measure the
complete Pi/ADC/DAC path. Check both host counters and Pi receive/send
underflow/overflow, conversion and xrun counters. A clean host result can still
hide a shortage inside the Pi. Physical converter timing needs an analog loop.
