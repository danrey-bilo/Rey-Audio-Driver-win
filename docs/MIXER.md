# Mixer for one USB card

The main page is **Mixer**. The top shows the connected USB card and its
permanent identity; no card selector or transport switch is present.
Choose Inputs 1–8 or Outputs 1–8 to edit the corresponding eight strips.

Each strip has gain −60…+12 dB, Mute, Solo, polarity inversion, a real PCM peak
meter and clip counter. Master applies to all output channels, −60…+6 dB.
Inputs affect recording before ASIO capture; outputs and Master affect
playback after ASIO render. Solo combines enabled solo channels in the same
direction. Unavailable audio has no fabricated meter activity.

Edits use allocation-free gain ramps and do not restart USB or add a PCM
queue. The one mixer/profile is saved in the service registry. Reset returns
to unity and clears Mute/Solo/polarity. The panel is a control client; closing
it to the tray does not stop the service or ASIO host.

USB settings and Diagnostics are the two other pages. The Windows Sound
button is disabled in the installed ASIO-only mode; ordinary Windows sound
endpoints require the [separate endpoint stage](ENDPOINTS.md).
