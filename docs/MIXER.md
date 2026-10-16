# Mixer for one USB card

The main page is **Mixer**. The top shows the USB card and connection status;
the permanent identity is under Diagnostics. No card selector or transport switch is present.
Inputs 1–8 and Outputs 1–8 are visible together, arranged as four adjacent
pairs per direction. There is no direction selector.

Each strip has gain −60…+12 dB, Mute, Solo, polarity inversion, a real PCM peak
meter and clip counter. Inputs affect recording before ASIO capture; outputs affect
playback after ASIO render. Solo combines enabled solo channels in the same
direction. Unavailable audio has no fabricated meter activity.

Every pair has an independent MONO / STEREO switch. MONO keeps both strips
independent. STEREO copies the left controls to the right and shows one shared
fader and Mute/Solo/polarity controls, with separate L/R meters. This links
controls; it does not sum audio channels. Splitting preserves both levels.
New profiles start at 0 dB, with all pairs independent. Existing levels and
links survive service/panel restarts; older profiles migrate without changing
their gains. Reset clears gain/Mute/Solo/polarity and preserves pair links.

Edits use allocation-free gain ramps and do not restart USB or add a PCM
queue. The one mixer/profile is saved in the service registry. Reset returns
to unity and clears Mute/Solo/polarity. The panel is a control client; closing
it to the tray does not stop the service or ASIO host.

**Device** and **Diagnostics** are the other pages. Device starts with board
mode and audio format; ASIO, channel routes and advanced queue settings are
collapsed. Mixer reset is visible below the two channel banks. Session counters
and the log are separate disclosures in Diagnostics.

The panel starts in the tray at login and keeps its icon available when USB
or the service is absent. The tooltip reports the actual connection state;
CDC board control and USB audio are distinct statuses. A single left click
opens the panel. Close/minimize keeps it in the tray; Exit stops only the
panel. Windows may place the icon in its overflow area. Visible mixer updates
use a 100 ms timer; hidden polling is reduced. Ordinary Windows sound endpoints remain a
[separate endpoint stage](ENDPOINTS.md).

The dev.6 update passed 43 native contracts, 23 board-control contracts and
the independent C# mixer-pair checks. Finite WPF checks cover both banks,
all eight links, normal/minimum sizes, the three pages and tray lifecycle.
The installed service passed link/edit/split/restore checks for all eight
pairs. The real USB digital loop is separate from the WPF fixture and does
not measure analog ADC/DAC latency. [Recorded checks](evidence/mixer-pairs-20261016.json).
