**English** | [Русский](MIXER.ru.md)

# Mixer and the USB sub-2-ms goal

Mixer is the home page. The card selector at the top changes the view only:
all configured cards keep their own sessions and settings. Select capture or
playback to display that card's eight strips. Software gain −60…+12 dB, Mute,
direction-specific Solo and PCM
polarity inversion act on actual audio. Master −60…+6 dB affects all eight
playback outputs. Channel mapping stays 1→1; this is not a stereo/Cue matrix.
No hardware preamp, phantom power or direct-monitor controls are simulated.

![Rey Audio Mixer](assets/rey-mixer.png)

Meters show real post-processing PCM peaks and software clipping counts. Idle
meters are empty. The screenshot is an actual WPF render with a finite service,
without installed ACX. UI edits are coalesced per channel; open mixer telemetry
updates up to 20 Hz, hidden tray polling at 1 Hz. Audio runs in the current
callback with fixed capture scratch space, in-place render, lock-free parameters,
no allocation/mutex or added PCM queue. A 64-sample gain transition does not
buffer/delay samples; saved mute applies immediately on restart. Unity preserves
PCM16/24/32 exactly. Mixer state belongs to each DeviceId and is persisted in
that card's registry profile. Live edits and display selection do not restart
streaming. Commands carry the card ID, including pending fader edits during
selection changes. Master applies to the selected card's playback channels.

The ACX implementation splits each 8×8 card into four Windows stereo input/output
pairs and full multichannel endpoints. [Names, routing and app use](ENDPOINTS.md).
Loaded Windows endpoints and ordinary-app audio still need qualification.

The goal is physical USB audio round-trip **below 2 ms**, not yet qualified.
Earlier 299s 8x8/192kHz/32-bit/depth3 digital transport RTT p50/p95/p99/max:
373/410/419/944.2 us, zero invalid PCM/missing indices/host timeouts. It excludes
installed Windows audio engine and converters. [Exact measured builds](https://github.com/danrey-bilo/Pi5-AUSB/blob/v0.1.0/docs/RESULTS.md).

The 2.7.0 finite manager test passed 12 checks, including live edits and three
format changes. Digital callback mean snapshots were 0.45–1.36 µs, maximum
168.9 µs; the longest 51.814s segment had a 1522.4 µs receive-gap maximum.
The separate 29.828s stream continued while an offline card was selected,
with a 574.8 µs maximum gap. Missing frames, layout underruns/overruns and USB
errors were zero. These functional checks used one physical Pi; they measure
continuity and callback processing, not end-to-end RTT. CPU load and gap
percentiles were not collected. [Evidence and limits](REY-AUDIO-2.7.md#evidence).

Two-block structural budgets: 16 frames at 44.1kHz = 0.726ms; 16 at 48kHz,
32 at 96kHz and 64 at 192kHz = 0.667ms. Packetization, USB, application, Pi and
ADC/DAC filters still add latency. Block64 at 44.1/48kHz already budgets about
2.90/2.67ms for two blocks. Supported ACX/WASAPI periods and real deadline
counters must be measured after loading. Buffers remain manual; USB depth3 is
the tested starting point for the digital transport, not an independent-clock
192kHz qualification. Remaining: ACX/Verifier/cancellation, WASAPI/DAW, hardware
clock feedback, ADC/DAC and physical loopback runs ≤300s. A 125us USB interval
is not an audio-latency result. Pi4 is unchanged.

[Installation](INSTALL.md) · [Options without Test Mode](INSTALL-NORMAL-WINDOWS.md)
