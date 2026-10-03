# Single-device USB control API

Local message pipe: `\.pipeReyAudio.Control.v1`. Requests are bounded ASCII;
regular commands return JSON. The service verifies local client ownership
for ASIO shared-memory handles. PCM travels through anonymous shared memory,
not this pipe. There is one USB session and one mixer.

| Request | Effect |
|---|---|
| `STATUS` | USB/ASIO state, identity, format, counters, mixer and recent events |
| `USB rate bits depth block guard automatic` | Save the manual USB profile; restart stream when its settings change |
| `MIX direction:channel gain mute solo polarity apply` | Live channel edit; direction 0=input, 1=output, channel 0–7 |
| `MASTER gain mute` | Live output Master edit |
| `MIX_RESET` | Unity gains, Mute/Solo/polarity disabled |

Gain is centi-dB biased by 6000: `6000` means 0 dB, `5800` means −2 dB.
Channel gain 0–7200; Master 0–6600. Flags are 0/1. USB rates are the six
supported rates, bits 16/24/32, depth 1–16, block 16/32/64/128/256, guard 0–8192.
Stop ASIO before applying a USB format. Live mixer edits do not restart USB.

Status includes `version`, `identity`, `device_limit:1`, `device_warning`,
`usb`, `active`, `stats`, `asio`, `mixer`, `events`. `route` can only be `usb`
or `none`. There is no `lan`, `preferred`, `devices` or `selected_device`.
Retired discovery, routing, card selection and scoped card commands are
rejected without saving settings. Errors return `ok:false` and `error`.

Internal ASIO commands are `INFO`, `OPEN`, `CLOSE`, `RATE`, with `auto` or the
exact currently owned USB identity. Exact identity prevents a stopped host
from accidentally reopening another card. It is not a multi-card selector.
One host owns the connection; PID and mapping/event handles are authenticated.
IPC ABI version 1 and strict structure size remain unchanged.

Service statistics reset on USB session restart; ASIO counters reset on host
connection. Track `generation` and `connection_id` when comparing snapshots.
A zero error counter alone is not an RTT, ADC/DAC or sustained cadence proof.
