**English** · [Русский](USB-LATENCY-2.8.1.ru.md)

# USB-ASIO 2.8.1: 96/192 kHz stability and latency

One Pi5 with digital loopback completed both final native profiles for
**175 seconds with zero PCM drops or render deadline errors**. RTT p99 is about
**1.53 ms**. Both final-code Ableton observations also passed 175 s each with
CPU Usage Simulator 50%: 192 kHz/64 and 96 kHz/32, lead3/depth4. ASIO reports a
buffer-model total of about 1.51 ms.
The strict condition “every measured RTT below 3 ms” **is not met**:
maxima were **4.3675 ms at 192 kHz** and **3.1292 ms at 96 kHz**.
ASIO's buffer model, digital RTT and physical ADC/DAC latency are separate measurements.

## Profiles observed in Ableton

| kHz | ASIO Samples | Render lead, blocks | USB packets | ASIO input + output, frames | Model total, ms |
|---:|---:|---:|---:|---:|---:|
| 192 | 64 | 3 | 4 | 160 + 129 | 1.5052 |
| 96 | 32 | 3 | 4 | 80 + 65 | 1.5104 |

These are manual profiles used in the native host and passing Ableton observations below;
a lightweight host passing does not qualify a loaded project. The Pi uses
**Pi5-AUSB 0.1.1**, FIFO40 and locked memory. Fresh Windows profiles use depth4;
saved values are retained. There is no automatic buffer tuning.

## Final installed-code digital RTT

RTT is measured from the native ASIO output callback to the returned input PCM
marker. Every ASIO block after a 50 ms warmup verifies all eight channels.
Long runs use PCM32 and 8×8. Each audio run is limited to 175 seconds.

| kHz | Block / lead / depth | Audio duration, s | RTT p50 / p95 / p99 / max, ms | Drops / late frames / missing frames / overflow |
|---:|---|---:|---|---|
| 192 | 64 / 3 / 4 | 175.012 | 1.3536 / 1.4298 / 1.5280 / 4.3675 | 0 / 0 / 0 / 0 |
| 96 | 32 / 3 / 4 | 175.005 | 1.3665 / 1.4227 / 1.5199 / 3.1292 | 0 / 0 / 0 / 0 |

Over 524,000 markers returned in each run, with zero invalid, duplicate or
overdue unreturned markers. Four final markers were still in flight at stop.
Backwards positions, MMCSS errors and USB-session changes were zero.

| kHz | Max callback gap / processing / capture age / service gap, us | Frames/s | Nominal deviation | Host CPU, one core |
|---:|---|---:|---:|---:|
| 192 | 3272.900 / 42.700 / 3279.600 / 3140.900 | 191864.4 | -0.0706% | 3.553% |
| 96 | 2125.100 / 45.500 / 2131.600 / 1874.200 | 95979.0 | -0.0219% | 4.723% |

Cadence uses the first-to-last callback span. A ±1% guard detects gross digital-bench
slowing; it is not an independent audio-clock measurement. GetProcessTimes CPU is
coarse and includes marker checking, not Ableton load. Windows service CPU access
was denied and is recorded as unavailable. Auxiliary Pi CPU spans include preparation
and restoration, are not audio-test durations and exclude IRQ/workqueue CPU.

## Actual Ableton

Observation verifies the Ableton PID, unchanged generation/connection_id/profile,
PCM meters and frame cadence. Counters are deltas inside the observation window.
This test **does not measure RTT**. CPU Usage Simulator is operator-controlled;
an earlier unrecorded value is not inferred.

| kHz | Block / lead | Simulator | Window, s | Drops / late / missing / overflow | Nominal deviation | Ableton CPU, one core | Result / code |
|---:|---|---|---:|---|---:|---:|---|
| 192 | 32 / 4 | not recorded | 175.000 | 28781 / 7844137 / 8765121 / 0 | -0.6007% | 237.054% | FAIL · before default update |
| 192 | 64 / 4 | 50% | 175.000 | 0 / 0 / 0 / 0 | -0.5113% | 121.759% | PASS · before default update |
| 192 | 64 / 3 | 50% | 175.000 | 0 / 0 / 0 / 0 | -0.3847% | 178.571% | PASS · final |
| 96 | 32 / 3 | 50% | 175.015 | 0 / 0 / 0 / 0 | -0.1857% | 91.055% | PASS · final |

Process CPU sums all Ableton threads and can exceed 100% of one core. It uses
GetProcessTimes, not Ableton's DSP meter or the Simulator setting.

DAW load matters: actual Ableton at 192 kHz/32 Samples dropped frames although the
light native host passed. It is not a stable recommendation for loaded Ableton.
A passing observation covers its duration and load, not every project, sleep/resume
or an absence of occasional latency spikes.

## Comparisons and rejected settings

Before the final defaults update, the ASIO/audio path was the same but the service
SHA256 differed. Those records are retained separately from final-EXE measurements.

| kHz | Block / lead / depth | Audio duration, s | RTT p50 / p95 / p99 / max, ms | Drops / late / missing / overflow |
|---:|---|---:|---|---|
| 192 | 64 / 4 / 3 | 175.011 | 1.6623 / 1.9312 / 2.0287 / 4.0028 | 0 / 0 / 0 / 0 |
| 96 | 16 / 4 / 4 | 175.013 | 1.1350 / 1.2789 / 1.3612 / 1.9242 | 0 / 0 / 0 / 0 |
| 192 | 32 / 4 / 4 | 175.015 | 1.1488 / 1.2900 / 1.3733 / 2.0847 | 0 / 0 / 0 / 0 |
| 96 | 32 / 4 / 4 | 175.008 | 1.6342 / 1.7792 / 1.8501 / 3.3725 | 0 / 0 / 0 / 0 |
| 192 | 64 / 4 / 4 | 175.009 | 1.6477 / 1.7903 / 1.8694 / 3.2480 | 0 / 0 / 0 / 0 |

- Native 192/32/4/depth4 and 96/16/4/depth4 had maxima 2.0847 and 1.9242 ms;
  their success does not qualify loaded Ableton.
- Larger blocks with lead4 had maxima 3.2480 and 3.3725 ms. Zero PCM counters
  alone do not establish a strict maximum below 3 ms.
- Depth3 at 192 kHz/block64/lead4 slowed cadence by about 2.12% and had a
  4.0028 ms maximum. It was rejected despite zero PCM errors.
- Preliminary 30 s successes are retained. The 175 s runs exposed higher tails;
  a short screen cannot rule them out.

Service gaps and capture age are recorded. Without hardware/ETW tracing, a rare
peak cannot be assigned specifically to an IRQ, USB controller or scheduler.
The next strict-maximum step is to localize those pauses. Larger buffers add
deadline reserve but do not remove delivery delay.

## Changes and checks

- Rearm Windows IN immediately after PCM/metadata moves into local storage,
  before mixer/IPC and OUT completion waits. Never reread the resubmitted slot.
- Pi gadget: FIFO40/reset-on-fork, CPU0, 32 MiB memlock and locking before streaming.
  Bench USB IRQ remains FIFO50. Process affinity does not pin every Pi kernel worker.
- The panel shows live versus selected ASIO settings, the reported buffer sum,
  reopening instructions and nonzero deadline counters. ASIO settings remain
  visible at the checked 1120×800 window size.
- The installer hashes all payloads and replaces only changed owned files.
  An unchanged DLL is not rewritten; a changed loaded DLL requires exiting the DAW.
- Windows contracts **32/32**; Pi5-AUSB **7/7 on Windows and 7/7 on Pi5**;
  installed Windows SDK consumer **0.1.1 EXACT, 1/1**. Final format checks:
  **6/6**, 96/192 kHz × PCM16/packed24/int32, 5 s each, block64/lead3/depth4.

Local EXE installation/upgrade and profile preservation passed. Uninstall, physical
hotplug/sleep/resume and ordinary Windows endpoints remain separate stages.
A 125 µs High-Speed interrupt interval is not end-to-end audio latency.
ADC/DAC is absent; analog latency is **unmeasured**. Other rates were outside
the below-3-ms qualification target. AoIP/Pi4 repositories are unchanged.

## Reproduce and inspect

Disable ASIO in the DAW. Use one digital-loopback Pi5 and a unity mixer.
These commands return failure when the strict 3 ms maximum is not met, even
with `functional=True` and zero PCM errors.

```powershell
python tools/test_usb_asio_profile.py --bin 'C:/Program Files/ReyAudio/USBASIO' `
  --out build/proof-192 --rate 192000 --block 64 --lead 3 --depth 4 `
  --seconds 175 --rtt-limit-ms 3 --dense-markers
python tools/test_usb_asio_profile.py --bin 'C:/Program Files/ReyAudio/USBASIO' `
  --out build/proof-96 --rate 96000 --block 32 --lead 3 --depth 4 `
  --seconds 175 --rtt-limit-ms 3 --dense-markers
python tools/test_installed_usb_asio.py --bin 'C:/Program Files/ReyAudio/USBASIO' `
  --out build/proof-formats --rates 96000 192000 --block 64 --lead 3 --depth 4
```

| Installed file | SHA256 |
|---|---|
| `ReyAudioService.exe` | `09593dbb95ea48ccecbecb8697297da7743d7f1f80380881fbec61412f90008c` |
| `ReyAudioControl.exe` | `aac4700f9825c647817e7c9c144589102db92e48d2b32a2d686ded63314ea064` |
| `ReyAudioAsio.dll` | `d459e5b0a1abd045f5c5fbe31446e34d8b826d49fc88ef9aba7d690fcc6f8392` |
| `ReyAsioProbe.exe` | `d1474d83c8e7ddf7bfa523d7d5a3d496453d86d865cc0f55690e73c95cd94925` |
| Installer EXE | `85be7c6115f0f114593c404c69bd0576ddeb2c4b57c976d1e4f782a435ffc9ab` |

[Complete results JSON](evidence/rey-usb-latency-281-20261003.json) ·
[Raw runs, installation, contract logs and CPU](evidence/usb-latency-2.8.1-20261003/) ·
[2.8.0 history](USB-ONLY-2.8.md) · [Install and Ableton](USB-ASIO.md).
