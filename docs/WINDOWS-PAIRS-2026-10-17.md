# Windows stereo pairs — dev.7 bench report

**English** · [Русский](WINDOWS-PAIRS-2026-10-17.ru.md)

One Rey installer now installs the unchanged signed SAR 0.13.1 kernel, Rey's
service, eight-channel ASIO and the existing mixer/tray. Four recording pairs and
four playback pairs appear as **Input / Output 1-2, 3-4, 5-6, 7-8 (Rey Audio)**.
No external routing program, SAR ASIO wrapper or network filter is required.

The package was installed on the bench PC, then installed again with the upstream
source archive included. All 11 installed payload hashes match; the saved binary
USB/mixer profile is unchanged. The signed root is running with PnP status OK,
the service starts automatically and the tray has one process. Existing 96 kHz,
PCM32 and Combined mode were restored after the rate checks.

## Validation

| Check | Result |
|---|---|
| Native CTest / board controls / mixer pair contract / installer compile | 44 passed / 23 passed / passed / passed |
| Synthetic kernel loop, all six rates × four pairs | 24 cases, 3 s each; zero steady wrong samples, duplicates, skips or discontinuities |
| Real USB/JACK, Windows-only at 44.1 and 192 kHz | Four pairs together at each rate, 5 s of PCM each; same zero steady counters |
| Real USB/JACK, Windows plus registered ASIO at 96 kHz | Four Windows pairs, 20 s of PCM; ASIO stream 45.019 s; same zero steady PCM counters and no ASIO drops/late/missing/overflow |
| Isolated JACK loop | All 8 ADC source connections restored; zero restore errors and zero JACK xruns |
| Idle after applications close | 10 s, zero callbacks/frames; WMI CPU counter reported 0.0% of one logical core |

Windows markers encode pair identity, opposite channel relationship and a
continuous sample sequence. Each stream retains its initial discontinuity and
startup mismatch count in the evidence; only the first **100 ms** Windows
fade-in is excluded from steady checks. The first real-USB ramp attempt failed
because ordinary ADC sources were added to the diagnostic return. The successful
fixture temporarily isolated those sources and restored every transient edge;
saved graph, presets and routes were not changed.

## Cold-start recording limitation

Separate 20-s capture-only IAudioClient3 runs at 44.1 / 192 kHz reported
**3 / 11 initial discontinuities** and **887 / 3,876 lost-frame positions**.
After an explicitly recorded **5-s warmup**, both final cases had zero position
gaps, repeated/lost positions and discontinuities for the remaining 15 s.
This does **not** qualify capture as uninterrupted from Start. An earlier
candidate also recorded a two-frame gap at 19.58 s; long-term reliability is
still open. All raw totals and individual flag times are retained in evidence.

Rey prepares USB on pin initialization with a bounded 300-ms warm window,
detects clients every 1 ms and publishes one shared WaveRT clock from native
frame counts. None of these policies adds a user setting. Windows clients must
still tolerate startup discontinuities; direct ASIO has separate qualification.

## Direct ASIO latency

The installed dev.7 service and registered DLL completed a separate **20.018 s**
real-USB digital JACK loop at **96 kHz / PCM32 / ASIO block16 / JACK32**.
All **738/738 markers** returned, with zero missing/invalid/duplicate markers,
capture drops, render late/missing frames, render overflow or backwards positions.

| RTT measurement, ms | p50 | p95 | p99 | max |
|---|---:|---:|---:|---:|
| PCM sample positions | 4.71875 | 4.71875 | 4.71875 | 4.71875 |
| Windows wall clock | 4.6285 | 4.7377 | 4.7426 | 4.8587 |

Pi native USB overflow/underflow, JACK-xrun, conversion and lease-timeout deltas
were zero. The diagnostic JACK helper recorded zero xruns, including startup. PC service
CPU was **77.30% of one logical core** during the latency test; Pi native USB/JACK/I/O
CPU was **18.73 / 10.67 / 8.90%** of one core. This was not an Ableton or physical ADC/DAC measurement.

## Limits and reproduction

The ordinary Windows shared engine reported a **10 ms period**; the ASIO RTT
above does not apply to that path. Real Windows-pair USB checks used PCM32.
Exclusive WASAPI, long DAW load, overnight stability, physical hotplug,
sleep/resume, production uninstall and analog ADC/DAC latency remain unqualified.

The original 2018 cross-signature loaded on Windows 11 build 26300 with Secure
Boot and Memory Integrity already off. No security/boot setting was changed.
Other signature policies are unverified. Windows endpoint IDs vary by rate;
reopen affected applications after changing the board rate.

[Evidence and hashes](evidence/rey-windows-pairs-20261017.json) ·
[Installation details](ENDPOINTS.md) ·
[Unmodified upstream runtime/source](../external/sar-0.13.1/README.md)

Build with the default `REY_ENABLE_SAR_BRIDGE=ON`. The finite
`ReySarFixture` / `ReySarSharedProbe` tools and `tools/sar/check_pairs.ps1`
reproduce the synthetic test without USB; they must not compete with the
installed endpoint-owning service. `tools/sar/jack_loop.cpp` is the separate
finite Pi diagnostic and restores its temporarily disconnected ADC sources.
