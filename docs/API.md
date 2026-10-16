# Single-device USB control API

Local message pipe: `\.pipeReyAudio.Control.v1`. Requests are bounded ASCII;
regular commands return JSON. The service verifies local client ownership
for ASIO shared-memory handles. PCM travels through anonymous shared memory,
not this pipe. There is one USB session and one mixer.

| Request | Effect |
|---|---|
| `STATUS` | USB/ASIO state, identity, format, counters, mixer and recent events |
| `FORMAT rate bits` | Save format with automatic per-rate transport parameters; refuse changes while ASIO runs |
| `USB rate bits depth block guard automatic` | Legacy admission; queue/guard are canonicalized to production policy |
| `MIX direction:channel gain mute solo polarity apply` | Live channel edit; direction 0=input, 1=output, channel 0–7 |
| `LINK direction:pair enabled gain mute solo polarity apply` | Link/split adjacent channels; pair 0–3; enabling atomically applies the supplied controls to both |
| `MASTER gain mute` | Live output Master edit |
| `MIX_RESET` | Unity gains, Mute/Solo/polarity disabled; stereo links preserved |
| `BOARD_MODE soundcard\|combined` | Save board rate policy; Combined clamps saved high rates to 96 kHz; rejected while ASIO runs |

Gain is centi-dB biased by 6000: `6000` means 0 dB, `5800` means −2 dB.
Channel gain 0–7200; Master 0–6600. Flags are 0/1. USB rates are the six
supported rates and bits 16/24/32. Legacy USB payloads admit depth 1–16,
block 32/64/128/256 and guard 0–8192, then normalize requests/guard for production.
Stop ASIO before applying a USB format. Live mixer edits do not restart USB.
`MIX` edits either member of a linked pair atomically. `STATUS.mixer` includes
four booleans each in `input_stereo` and `output_stereo`. Saved profile format 5
retains both masks and reads earlier formats 1–4 without changing channel gains.
The control thread reaps exited ASIO clients even while USB callbacks are idle.

Status includes `version`, `identity`, `device_limit:1`, `device_warning`,
`usb`, `active`, `stats`, `asio`, `mixer`, `events`. `route` can only be `usb`
or `none`. There is no `lan`, `preferred`, `devices` or `selected_device`.
Retired discovery, routing, card selection and scoped card commands are
rejected without saving settings. Errors return `ok:false` and `error`.

Internal ASIO commands are `INFO`, `OPEN`, `CLOSE`, `RATE`, `CAN_RATE`, with `auto` or the
exact currently owned USB identity. Exact identity prevents a stopped host
from accidentally reopening another card. It is not a multi-card selector.
One host owns the connection; PID and mapping/event handles are authenticated.
IPC ABI is version 2 with 64 ring slots; service and DLL must be updated together.

Service statistics reset on USB session restart; ASIO counters reset on host
connection. Track `generation` and `connection_id` when comparing snapshots.
A zero error counter alone is not an RTT, ADC/DAC or sustained cadence proof.

`STATUS.board_mode` is the local saved policy. `BOARD_MODE` is not a remote Pi
command: the panel separately confirms `set_operating_mode` over Rey Effects
CDC before saving this policy. See [AKM modes](AKM-OPERATING-MODES.md) for the
USB prerequisite, two PC choices and automatic standalone state.
