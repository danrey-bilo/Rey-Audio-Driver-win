**English** | [Русский](ENERGY-SAVING.ru.md)

# Channels, digital silence and tray

Use **Device → Channels** to select inputs/outputs, then **Done → Apply**. The physical channel count is reported by the Pi. An unchecked channel retains its physical ASIO number but contributes no selected network PCM in V3. Settings are saved in the user INI.

**Settings → Advanced → Reduce traffic during digital silence** enables two behaviors on a V3 peer:

- Select the intersection of manually enabled channels and host-created ASIO buffers.
- Omit exact integer-zero PCM while preserving every nonzero sample value.

An entirely silent direction emits three transition markers, then stops audio datagrams. The first nonzero block resumes within the existing session. ASIO callbacks continue while the host keeps the device started. With reduction disabled, manually enabled channels continue to be transported during start. Normal stop unsubscribes; an abandoned V3 session expires after three seconds. Keepalive and tray discovery are control traffic.

These features need V3 support. Pi 5 uses the demand-driven service; Pi 4 retains its legacy continuous wrapper. See [protocol V3](https://github.com/danrey-bilo/AoIP-lib/blob/main/docs/PROTOCOL-V3.md).

`PiAoipControl.exe --tray` monitors connection state. Its icon appears when a Pi is found; click it to open settings and use the right-click menu to exit. `--quit` asks the existing tray instance to close. The MSI configures tray startup at sign-in. Version 2.4.3 uses embedded multi-resolution icons for the EXE, panel, tray, shortcuts and Installed apps.
