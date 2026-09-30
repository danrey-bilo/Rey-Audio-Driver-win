**English** | [Русский](RELEASE-2.4.3.ru.md)

# PiAoIP 2.4.3 — Windows ASIO driver

Development preview · `v2.4.3` · 2026-09-30

Fixes blank application and Start menu icons with embedded multi-resolution Settings and Installation Guide resources. The executable and DLL expose version 2.4.3. The compact English panel keeps sample rate, bit depth, ASIO buffer and LAN buffer on its main page; discovery, channels and diagnostics stay in the top menus. Automatic buffer tuning is fully removed. Existing manual profiles are preserved.

## Downloads

| File | Purpose |
|---|---|
| `PiAoIP-2.4.3-Windows11-x64.msi` | Recommended Windows installer; includes driver, settings, guide and licenses |
| `Win11-asio-AoIP-2.4.3-source.zip` | Driver/panel source; external AoIP and ASIO SDK access required |
| `SHA256SUMS.txt` | Checksums for all release files |
| `LICENSE.txt` | Project license terms |

## Installation

Close Ableton/other ASIO hosts and exit PiAoIP from the tray before running the MSI. Select **Pi AoIP** in a 64-bit DAW after installation. The INI stays at `%LOCALAPPDATA%\PiAoIP\PiAoipAsio.ini`. The MSI is unsigned. The source archive excludes the private AoIP-lib dependency and the external Steinberg SDK.

## Validation

Fresh Windows x64 Release build passed four CTest checks. Twelve invisible dialog renders covered six pages at two font scales, including navigation, visibility and footer bounds. Windows Shell extraction confirmed two EXE icon groups and one DLL icon group. MSI inspection confirmed version 2.4.3, English UI, five payload files, icon indices 0/1 for the two shortcuts, an Installed apps icon and no auto-tuning helper.

Release preparation did not install the MSI or new DEBs, restart the running Pi service or change the Windows profile. No new long-duration DAW or physical ADC/DAC test was performed. The peer remains synthetic PCM; this release does not claim guaranteed sub-millisecond or physical converter latency.

## Licenses

Personal noncommercial use is free; commercial use requires a separate written license. The ASIO component is additionally subject to Steinberg terms.
