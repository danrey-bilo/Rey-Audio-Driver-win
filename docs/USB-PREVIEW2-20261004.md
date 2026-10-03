**English** · [Русский](USB-PREVIEW2-20261004.ru.md)

# USB preview 2: clean Pi and simplified mixer

**2.8.1-usb-preview.2**, 4 October 2026. One Rey Audio USB board, 8 inputs and
8 outputs. The Windows 2.8.1 audio core and Pi5-AUSB 0.1.1 runtime retain their
tested binaries; the panel and installer are updated.

## Raspberry Pi

The retired `aoip_peer_rpi5` was still running at FIFO 70 on CPU0, alongside
AUSB at FIFO 40. This was competing load, not proof of the old latency-tail cause.
Both AoIP packages/services, configuration/state, service account, four legacy
builds and `Pi5-AoIP-LAN` profile were removed. A wider installation/configuration/
package/project scan found no remaining AoIP files or process. System journal
history was retained. The old configuration backup is on the PC.

RT Linux **6.18.50+rpt-rpi-v8-rt** and boot files are unchanged. AUSB kept PID
**11663**, FIFO 40, CPU0 and locked memory; the USB controller stayed `configured`.
No reboot or AUSB restart occurred. Wi-Fi/SSH and the stock Ethernet profile remain.

## Windows panel

- **Mixer:** eight input or output strips, Master, Mute/Solo/polarity.
- **Settings:** ASIO buffer and calculated latency, sample rate and PCM bits.
  ASIO reserve and USB queue are under **Advanced**; values stay manual.
- **Diagnostics:** counters and log. Persistent DAW/background instructions,
  the tray button and unavailable system-sound button have been removed.

The icon appears when the service detects the USB board, without opening a
window. Losing the board/service hides it; detection restores it. Closing or
minimizing the connected panel keeps it in the tray. Repeated `--tray` startup
does not activate the window. The context-menu exit closes only the panel.

Installation starts the panel immediately; login starts it again. A normal
installer process delegates privileged changes through UAC, then starts the
panel as the user. No boot/security changes are made. Windows chooses whether
the icon is visible in the main notification area or its overflow menu.

USB format changes are disabled while ASIO runs. Saved buffer/reserve changes
take effect when the audio engine reopens; a short hint appears only when live
and selected values differ. ASIO buffer calculation remains distinct from RTT;
details are in its tooltip.

## Validation

Real WPF/NotifyIcon checks cover six board/service-state transitions, closing
and minimizing. These are injected UI states, **not physical hotplug**. Live and
disconnected renders passed at 1120×800 and a minimum 1100×700 window, whose
client area was 1085.6×662.4 DIP. Core controls remain visible.
Full EXE installation and `--panel-only` update returned 0 locally and started
the panel automatically with its window hidden. Panel-only update leaves the
service/ASIO DLL unchanged. Full installation may restart the service; stop the
audio session before a full update.

After retirement, PCM32/8×8 **short native digital smoke checks** passed:

| kHz | Block / reserve / queue | Audio, s | RTT p50 / p95 / p99 / max, ms | Drops / late / missing / overflow |
|---:|---|---:|---|---|
| 192 | 64 / 3 / 4 | 30.007 | 1.3618 / 1.4117 / 1.4421 / 1.7551 | 0 / 0 / 0 / 0 |
| 96 | 32 / 3 / 4 | 30.012 | 1.3677 / 1.4195 / 1.4372 / 2.0709 | 0 / 0 / 0 / 0 |

| kHz | Max callback gap / processing / capture age / service gap, µs | Cadence error | Host CPU, one core |
|---:|---|---:|---:|
| 192 | 680.400 / 27.200 / 646.800 / 469.000 | -0.0104% | 0.208% |
| 96 | 749.200 / 16.700 / 746.900 / 474.900 | -0.0044% | 0.104% |

Invalid/duplicate/overdue markers were zero. Four final markers remained in
flight at each stop. Cadence stayed within ±1%. GetProcessTimes CPU has coarse
resolution. ASIO preferences were untouched and temporary USB rate was restored.
These 30 s checks do not replace 175 s measurements or a loaded Ableton check;
[earlier peaks above 3 ms](USB-LATENCY-2.8.1.md) remain documented. Equal-load causal
comparison and physical ADC/DAC measurement were not performed.

## Binaries

| File | SHA256 |
|---|---|
| `ReyAudioService.exe` | `09593dbb95ea48ccecbecb8697297da7743d7f1f80380881fbec61412f90008c` |
| `ReyAsioProbe.exe` | `d1474d83c8e7ddf7bfa523d7d5a3d496453d86d865cc0f55690e73c95cd94925` |
| `ReyAudioControl.exe` | `b2222a863627e586143bf94043bd8604ad962201916bbaa86298e596ba21f7bc` |
| `ReyAudioAsio.dll` | `d459e5b0a1abd045f5c5fbe31446e34d8b826d49fc88ef9aba7d690fcc6f8392` |
| Installer | `f3298e21c44f6a1e5dcdae5f52804416e5a093c08a1ae87e59f5d4ed56e715f7` |

[Summary](evidence/usb-only-cleanup-20261004.json) ·
[Raw native/UI results](evidence/usb-preview2-20261004/) · [Installation](USB-ASIO.md).
