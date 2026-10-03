**English** | [Русский](REY-AUDIO-2.7.ru.md)

# Rey Audio Driver 2.7.0 preview

The service now owns independent sessions by permanent DeviceId. The panel's
card selector is a view selector. Each 8×8 board has four stereo pairs per
direction and a full multichannel endpoint per direction in the ACX code.
[Windows devices and usage](ENDPOINTS.md).

![Mixer with a real detected Pi](assets/rey-mixer.png)

## Structure

| Module | Ownership |
|---|---|
| `service/manager` | Bounded card catalog, identity association, display selection, USB discovery and LAN port validation |
| `service/device_session` | One board's worker, routing/retry, mixer, callback counters and events |
| `service/device_store` | Validated ID paths, per-board registry profiles, selected view and forgetting offline profiles |
| `service/commands` | Strict shared parser for settings commands |
| `audio/mixer` | Fixed buffers, atomic gains, meters and gain transitions |
| `engine/usb_session` | Opens the exact known USB path and verifies its ID; avoids opening other active cards |
| `drivers/Acx/device` | Up to ten owned children, one service file handle per board, stable containers and asynchronous detach |
| `drivers/Acx/circuit` | Distinct full/pair circuits, channel masks and RAW/default/communications/media/movie modes |
| `drivers/Acx/stream` | Separate WaveRT stream slots and positions per endpoint, bounded PCM exchange under each board's lock |
| `include/piaoip/endpoints.h` | Shared bounded mapping and valid-bit saturation, also exercised by host tests |

The root ownership lock is released before PCM work. A child has its own PCM
lock. Closing a board's service handle detaches its child only. Capture pairs
are consumed before the aliased buffered IOCTL storage is overwritten by the
render reply. Full/pair render adds to the board channels; no new layout queue
is added. Kernel cancellation, PnP teardown and the complete lifecycle still
require Driver Verifier on a prepared machine.

The 64-bit registry layout is
`HKLM\Software\ReyAudio\Devices\<32-character-ID>\Profiles`; each value is the
existing bounded RYS1/v2 profile/mixer codec. `SelectedDevice` stores the view
independently. Existing single-card preferences migrate to the first matching
board. Subsequent boards receive their own settings. Console tests use isolated
binary files with the same codec; installed users configure everything in the
panel. No INI is installed and no automatic buffer tuning is added.

`DEVICE <ID> <command>` addresses an explicit card. `SELECT <ID>` persists only
the view. `ADD_LAN` reads identity before association; configured LAN remains
the default for that card. The same ID over two transports produces one session,
with one active route. Failed requested LAN is retried until the user selects
USB or disconnects LAN; automatic failover is not implemented. A route/profile
change restarts its own worker; mixer edits and display selection do not.

## Evidence

| Check | Result | Limit |
|---|---|---|
| Native contracts | 24/24 | DSP, codec, channel mapping, catalog/selection/isolation/persistence/ports; not loaded kernel |
| Finite real-Pi manager | 12/12; 20s production + 55s digital | USB discovery, explicit missing driver, same-generation selection, scoped mixer, profile changes and reconnect |
| Other-card view | 30s; passed | One real Pi plus one offline fixture; not simultaneous physical boards |
| WPF render | 1120×800; all eight strips visible | Actual service and detected Pi; driver unavailable is shown |
| Kernel package | INF verification/catalog generation passed | SYS built, not loaded |
| MSI/EXE | ICE, seven extracted payload hashes, exact embedded MSI and launch conditions passed | Elevated install/repair/uninstall not qualified |
| Local signer | New SYS/CAT signed, catalog members verified, private key deleted, damaged SYS rejected | No machine trust or boot policy changed in tests |

Exact reports: [manager](evidence/rey-manager-cards.json),
[view switching](evidence/rey-card-selection.json),
[installer](evidence/rey-installer-cards.json), [panel](evidence/rey-cards-ui.json).

The manager's digital callback mean snapshots were **0.45–1.36 µs**, with a
maximum snapshot of **168.9 µs**. It had zero missing frames and all USB run
summaries had zero layout underruns/overruns and USB errors. The longest segment
was 51.814s; its maximum receive gap was **1522.4 µs**. The separate 29.828s
view-switching stream completed 229665 callbacks/5511960 frames, maximum receive
gap **574.8 µs**, with the same zero error counters. These are callback/continuity
results, not RTT or physical audio latency. Gap percentiles and CPU load were
not collected by these functional tests.

The earlier qualified digital transport run remains 299s, 8×8/192kHz/PCM32/depth3,
RTT p50/p95/p99/max 373/410/419/944.2 µs. The below-2-ms USB audio target applies
to ASIO/Ableton. A [local USB/service ASIO frontend](USB-ASIO.md) is now implemented
outside this published installer; a device-clocked backend and ADC/DAC loopback
remain. Shared Windows audio has a separate latency
budget. [Local signed endpoint backend](GATEWAY.md). Separate boards are not
yet a clock-synchronized aggregate device.

## Build and install

Use the [MSI/EXE guide](INSTALL.md). The package keeps unique free local test
signing. **Windows Test Mode is required.** Secure Boot changes and reboot are
explicit user actions; MSI does not disable Core Isolation. Preparation and
installation are not silently performed by development tests.

The native CMake build and WDK/WiX procedure are described in the
[build guide](REY-AUDIO-2.6.md#build). Run `ctest --output-on-failure` on the new
build. `tools/test_manager.py` and `tools/test_card_selection.py` perform bounded
console checks and never install a driver. The 2.7.0 installer has no external
signing-tool dependency on the client. Pi4 and the USB wire protocol are unchanged.
