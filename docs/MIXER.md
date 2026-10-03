**English** | [Русский](MIXER.ru.md)

# Mixer and the USB sub-2-ms goal

Mixer is the home page, with eight capture and eight playback strips selected
by direction. Software gain −60…+12 dB, Mute, direction-specific Solo and PCM
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
PCM16/24/32 exactly. Mixer state belongs to the one device and is persisted in
the registry independently of transport profiles; edits do not restart streaming.

The goal is physical USB audio round-trip **below 2 ms**, not yet qualified.
Earlier 299s 8x8/192kHz/32-bit/depth3 digital transport RTT p50/p95/p99/max:
373/410/419/944.2 us, zero invalid PCM/missing indices/host timeouts. It excludes
installed Windows audio engine and converters. [Exact measured builds](https://github.com/danrey-bilo/Pi5-AUSB/blob/v0.1.0/docs/RESULTS.md).

The newer finite manager test passed live mixer edits and three format changes,
without missing frames. Digital callback mean snapshots were about 0.6–1.7 us,
maximum up to 237.8 us, including possible scheduling interruptions; this is not
end-to-end RTT. [Evidence](evidence/rey-manager-mixer.json).

Two-block structural budgets: 16 frames at 44.1kHz = 0.726ms; 16 at 48kHz,
32 at 96kHz and 64 at 192kHz = 0.667ms. Packetization, USB, application, Pi and
ADC/DAC filters still add latency. Block64 at 44.1/48kHz already budgets about
2.90/2.67ms for two blocks. Supported ACX/WASAPI periods and real deadline
counters must be measured after loading. Buffers remain manual; USB depth3 is
the tested starting point for the digital transport, not an independent-clock
192kHz qualification. Remaining: ACX/Verifier/cancellation, WASAPI/DAW, hardware
clock feedback, ADC/DAC and physical loopback runs ≤300s. A 125us USB interval
is not an audio-latency result. Pi4 is unchanged.
