# Pi5-AUSB transport

Rey Audio Driver 2.8 uses only [Pi5-AUSB](https://github.com/danrey-bilo/Pi5-AUSB).
The Windows service opens one exact USB interface, verifies permanent DeviceId
and adapts packed PCM to interleaved signed samples. 8×8, 16/24/32 bits and
six sample rates 44.1–192 kHz fit its High-Speed interrupt profile. The Windows
build has no network dependency or route preference.

Transfers use bounded overlapped WinUSB slots. The protocol carries epoch,
sequence and absolute frame indices; malformed packets or timeline errors
stop the session. USB queue depth is manual. It must be evaluated together
with ASIO block and render lead, under the intended workload.

125 µs bus microframes do not imply 125 µs end-to-end audio. Queueing, Windows
completion delivery, ASIO scheduling and converter/backend delay also matter.
The current bench is a digital loopback. [Exact build report](USB-ONLY-2.8.md).

The service detects appearance/removal and retries; after a lost stream,
reopen ASIO in the DAW. A second card cannot replace the owned interface.
Multiple-device aggregation has been removed. A second physical board,
cable removal/reconnect, sleep/resume and device-clocked ADC/DAC are not
qualified by simulated selection tests or the digital-loopback runs.
