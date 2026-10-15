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

**Device** and **Diagnostics** are the other pages. Device starts with board
mode and audio format; ASIO, channel routes and advanced queue settings are
collapsed. Master and mixer reset are under **Advanced**. Session counters
and the log are separate disclosures in Diagnostics.

The panel starts in the tray at login and keeps its icon available when USB
or the service is absent. The tooltip reports the actual connection state;
CDC board control and USB audio are distinct statuses. A single left click
opens the panel. Close/minimize keeps it in the tray; Exit stops only the
panel. Windows may place the icon in its overflow area. Visible mixer updates
use a 100 ms timer; hidden polling is reduced. Ordinary Windows sound endpoints remain a
[separate endpoint stage](ENDPOINTS.md).

The 16 October 2026 update passed 21 C# contracts and finite WPF rendering
checks for all three pages at normal/minimum window size, ASIO disclosure,
six service/USB lifecycle cases, close and minimize. These use fixture state
and do not demonstrate physical USB audio. The panel was installed with
payload hash verification; the service, ASIO DLL and saved preferences were
preserved. Control executable file version: `2.8.2.2`.

[Recorded UI checks](evidence/ui-20261016.json)

![Device page rendered by the WPF fixture; not a physical USB test](assets/device-20261016.png)
