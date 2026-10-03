**English** | [Русский](ENDPOINTS.ru.md)

# Cards and Windows audio devices

Each Rey board has its own mixer, USB/LAN profiles and audio session. All
configured cards run concurrently. The selector at the top of **Mixer** changes
the displayed card only; it does not select which card is allowed to stream.
The current catalog holds up to ten cards, including saved offline cards.

## Windows channel pairs

For an 8×8 board, the ACX implementation creates ten logical audio endpoints:

| Capture | Playback | Board channels |
|---|---|---|
| Rey Audio `<ID>` Input 1/2 | Rey Audio `<ID>` Output 1/2 | 1 and 2 |
| Rey Audio `<ID>` Input 3/4 | Rey Audio `<ID>` Output 3/4 | 3 and 4 |
| Rey Audio `<ID>` Input 5/6 | Rey Audio `<ID>` Output 5/6 | 5 and 6 |
| Rey Audio `<ID>` Input 7/8 | Rey Audio `<ID>` Output 7/8 | 7 and 8 |
| Rey Audio `<ID>` Input 1-8 (Multichannel) | Rey Audio `<ID>` Output 1-8 (Multichannel) | All eight |

`<ID>` is the last eight characters of the board's permanent DeviceId. The full
32-character ID owns the session and the Windows device container. Two boards
with different IDs remain separate even when their sample formats match.

These are normal Windows capture/playback devices, intended for the Windows
Sound settings, Device Manager, browser microphones and communication apps.
They do not require an ASIO host. Select **Input 1/2** as the microphone in
Windows or the application; select an output pair for playback. The panel's
**Windows sound** button opens the system sound settings. Windows microphone
privacy permission still applies. Setup does not change the PC's default
speakers or microphone automatically.

Choose the full multichannel endpoint in a host that supports WASAPI/KS and
eight channels. DAW backend support and its own buffers determine the available
latency. Shared audio can resample an application's format to the board format;
supported periods and actual shared/exclusive streaming must be checked after
driver installation. Low transport latency does not guarantee sub-2-ms audio
in every browser or messaging application.

Capture pairs read the corresponding channels from the same current block.
Playback pairs write to their channel ranges. The full playback endpoint and
a pair may run together: their samples add with saturation at the valid PCM
width. Lower the source levels if their sum clips. The card's output mixer and
Master then apply once. No extra PCM queue is inserted for splitting pairs.

## Connect and switch

1. USB discovery associates a supported interface with its verified DeviceId.
2. For LAN, enter the Pi address on **AoIP / LAN** and press **Connect**. The
   service reads identity before adding or updating the card.
3. The same ID over USB and LAN updates one card. Configured LAN is the default;
   **Use USB** changes the route of that card. Route changes restart that card's
   session; other cards continue.
4. Each active LAN card requires a different local UDP port. The service rejects
   port conflicts and updates only its own Private/LocalSubnet firewall rule.
5. The top selector changes only the view. USB, LAN and mixer edits are addressed
   to the shown card. To remove an unused saved profile, disconnect it and use
   **Diagnostics → Forget disconnected card**.

The service uses separate clocks/timelines for separate boards. Concurrent
devices are not a synchronized aggregate audio interface. Clock synchronization
and drift compensation for aggregation remain separate work.

## Verification boundary

The driver builds, INF/catalog checks pass, and PCM pair mapping/saturation are
covered by native tests. The finite stand has **one physical Pi**. A 30-second
test switched the displayed mixer to an offline saved-card fixture while the
real USB stream continued with the same session generation and independent
settings. This is not a test with two physical boards.

**ACX has not been loaded on the current PC.** Appearance and naming in Windows,
simultaneous real endpoints, Chrome/Telegram capture/playback, kernel lifecycle,
Verifier, sleep/resume and physical ADC/DAC latency remain qualification gates.
The screenshot shows the actual panel with one Pi and explicitly reports the
unavailable driver. The current package is a **Pre-release**.

[Installation](INSTALL.md) · [Architecture and evidence](REY-AUDIO-2.7.md) ·
[Microsoft ACX enumeration](https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/acx-device-enumeration)
· [ACX streaming](https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/acx-streaming)
