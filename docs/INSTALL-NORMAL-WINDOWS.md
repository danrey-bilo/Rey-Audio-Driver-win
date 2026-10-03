# Install the USB version

Run **Rey-Audio-USB-ASIO-Setup-x64.exe**, accept UAC.
[Detailed instructions and Ableton fields](USB-ASIO.md).

One USB device connects automatically. Open
`%ProgramFiles%\ReyAudio\USBASIO\ReyAudioControl.exe` for the mixer/settings.
The service starts at boot and the tray at login. No INI or manual INF install
is required. No custom SYS, certificate or Test Mode is used.

Fully exit the DAW before updating. Earlier USB/mixer preferences migrate;
retired network settings and card selections are inactive. The package gives
ASIO 8×8; ordinary Windows endpoints are a separate unfinished stage.

The earlier public ACX 2.7 MSI is a historical package, not the current USB-ASIO
installer. Its signing/boot instructions do not apply to this EXE.
