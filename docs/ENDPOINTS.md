# Ordinary Windows audio endpoints

The current delivered USB-ASIO EXE gives Ableton/ASIO eight inputs and eight
outputs. It does **not** install system microphones/speakers for Chrome or
Telegram. The Mixer identifies this mode explicitly; Windows Sound is disabled.

The required user flow is one Rey installer, automatic device startup and all
controls in Rey. Four stereo recording endpoints and four stereo playback
endpoints map to 1–2, 3–4, 5–6 and 7–8. Linking MONO/STEREO controls in the
mixer is independent of creating those system endpoints.

## Native Rey backend

The ACX source now exposes exactly those eight stereo endpoints, without the
additional 1–8 endpoints. Its bridge has independent capture/render phase
clocks so native AUSB capture publication and later render collection do not
advance a stream twice. Bridge ABI 2 requires matching service and kernel
components. The package builds and passes INF/catalog checks; its SYS and CAT
are unsigned and were not loaded. Passing mapping contracts is not a loaded
Windows audio-driver or WASAPI qualification.

## Ready-made driver integration

[Thin Audio Gateway](https://software.muzychenko.net/en/thin-audio-gateway/)
provides a direct shared-buffer API controlled by the host application. It can
serve as an embedded driver component with the Rey service owning the audio
clock and routing, without a separate routing application. The vendor offers
custom line layouts. A signed package supporting eight stereo lines is needed
before this can satisfy the four-pair requirement. The earlier local demo
qualified one multichannel recording/playback pair, not four stereo pairs;
additional line creation failed. It is not a production eight-endpoint package.

An official VB-Audio Matrix trial was installed and its Microsoft-signed kernel
driver loaded during the 16 October investigation. Its application opened Rey
ASIO, which prevents an independent host from opening the same direct ASIO
connection. Full four-pair routing and latency were not qualified. The trial
was removed after the user required an integrated solution. Matrix is not a
Rey dependency, payload or autostart component.

The delivered USB-ASIO setup still contains no system endpoint driver. To
deliver the complete single-installer flow, supply a production-signed native
Rey package or a suitable signed OEM component and qualify its installation,
eight-channel routing, simultaneous Windows/ASIO operation and reconnects.
The existing ASIO path remains separate from Windows shared-audio buffering.
