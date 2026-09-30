**English** | [Русский](API.ru.md)

# ASIO host and driver API

## Configuration

Open the control panel from the running ASIO host for live counters, or use `PiAoipControl.exe` for standalone settings. The per-user file is `%LOCALAPPDATA%\PiAoIP\PiAoipAsio.ini`. `PIAOIP_CONFIG_PATH` selects a separate test profile; an existing adjacent INI can provide portable configuration. An empty/`auto` peer selects a single discovered device; ambiguous discovery requires a manual choice.

Apply validates the device profile, reconciles a lost control acknowledgement with rediscovery, atomically saves the Windows profile and requests a host reset when applicable. The standalone panel does not run an audio stream or read counters from another process. Buffers are manual.

## Host lifecycle

1. Initialize COM and create the driver `IASIO` instance. CLSID: `{A24D50B2-9111-4A6B-9C29-A01D617BC830}`. This implementation uses the same identifier for its driver interface.
2. Call `init()`, then query actual channels, rate, buffer sizes and channel types.
3. Create per-channel `ASIOBufferInfo`, install callbacks and call `createBuffers()`.
4. Call `start()` and check subscription/start errors.
5. In the callback, process the indicated half of the double buffer and call `outputReady()` after completing outputs.
6. Finish with `stop()`, `disposeBuffers()`, `Release()` and COM cleanup.

Check every `ASIOError`; use `getErrorMessage()` for details. ASIO channel indices are zero-based, UI channel numbers one-based. Keep a directly loaded DLL open until all objects are released. Do not allocate, access files/network or display UI inside a real-time callback.

| Wire PCM | ASIO type | Host container |
|---|---|---|
| 16 bit | `ASIOSTInt16LSB` | signed int16 |
| 24 bit | `ASIOSTInt32LSB24` | 24 valid bits in int32 |
| 32 bit | `ASIOSTInt32LSB` | signed int32, not float |

Wire PCM is interleaved; ASIO buffers are per channel. `bufferSwitchTimeInfo` is preferred when supported. A host that does not use `outputReady()` follows the deferred output path and can add an ASIO cycle. `setSampleRate()` alone does not replace a confirmed device-profile change.

## Diagnostics and scope

Use `future(aoip::diagnostics_selector, &snapshot)` and `stream_diagnostics_selector` with initialized versioned structures from [AoIP diagnostics.hpp](https://github.com/danrey-bilo/AoIP-lib/blob/main/include/aoip/diagnostics.hpp). Unsupported selectors are errors. Counters cover missing/late frames, deadlines, RX/TX queues, errors/expiry, host overruns and stream activity.

One streaming ASIO client is supported per Pi/PC pair. Multiple DAWs can conflict over UDP 50021. Windows shared microphone/speaker endpoints and a kernel audio driver are not supplied.

## Source modules

| Directory | Responsibility |
|---|---|
| `src/driver` | IASIO, double buffers, RX/audio/TX workers and Timeline |
| `src/config` | INI and profile persistence |
| `src/control` | Discovery and profile/session commands |
| `src/platform` | Windows timing, MMCSS and scheduling |
| `src/ui` | Native dialog, English labels, icons and resources |

These internal modules are not a separate stable ABI. Use IASIO for host integration and AoIP core for portable transport code.
