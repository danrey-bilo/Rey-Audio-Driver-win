# Mixer for one USB card

The main page is **Mixer**. The top shows the USB card and connection status;
the permanent identity is under Diagnostics. No card selector or transport switch is present.
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

Settings and Diagnostics are the two other pages. Advanced buffer/queue settings
are collapsed by default. The tray icon appears automatically when the service
detects the board. Ordinary Windows sound endpoints remain a
[separate endpoint stage](ENDPOINTS.md).
