**English** | [Русский](INSTALL.ru.md)

# PiAoIP 2.4.3 — Windows installation

PiAoIP connects a 64-bit ASIO host on Windows 11 x64 to a compatible Raspberry Pi service. It does not create Windows microphone/speaker endpoints. The panel, tray, installer and installed help use English.

## Install and connect

1. Close ASIO hosts and exit the PiAoIP tray icon.
2. Run `PiAoIP-2.4.3-Windows11-x64.msi`. Windows requests administrator access for installation, ASIO registration and a local-subnet UDP 50021 inbound rule.
3. Connect the PC and Pi over Gigabit Ethernet and configure reachable IPv4 addresses. Example dedicated subnet: PC `192.168.50.1/24`, Pi `192.168.50.2/24`, no Ethernet gateway/DNS. These are examples, not driver defaults.
4. Configure the PC address on the Pi with `sudo piaoip-configure --interface eth0 --peer 192.168.50.1 --restart`.
5. Open **PiAoIP Settings → Device → Find and connect**. Discover or connect to the Pi IPv4 address.
6. Set **Sample rate**, **Bit depth**, **ASIO buffer** and **LAN buffer**, then **Apply**.
7. Select **Pi AoIP** in your DAW. Reopen its audio engine after changing settings when the host does not handle the ASIO reset request.

## Panel menus

| Menu | Functions |
|---|---|
| Device | Find/connect, select individual input/output channels |
| Settings | Main audio settings, traffic reduction during digital silence |
| Diagnostics | Stream counters and digital RTT |

The physical channel count comes from the Pi. Select channels without changing that count. Buffers are manual; no automatic tuning helper is installed. Duration labels use `samples × 1000 / sample rate` and are not measurements of the complete audio path.

Live counters belong to the running ASIO instance. Open its panel from the DAW to view them. RTT requires PCM32 and a DAW route from an enabled input to its matching output without effects; avoid an acoustic feedback loop.

## Settings, upgrade and recovery

The profile is `%LOCALAPPDATA%\PiAoIP\PiAoipAsio.ini`. Existing values are preserved by the upgrade. Back up the file with ASIO hosts closed if you need an exact rollback. Change buffers manually and check the real project for missing/late frames and deadline misses.

Use **Installed apps → PiAoIP** to repair or uninstall. Version 2.4.3 includes embedded settings/guide icons, explicit Start menu shortcut icons and an Installed apps icon. Windows may retain an existing shortcut image briefly until Explorer refreshes it.

Windows ARM64, Windows Server and 32-bit ASIO hosts are outside this package. The Pi needs its separate service and an RT kernel. The MSI does not configure NICs, CPU affinity, Windows power settings, virtual machines or the Pi OS.

## Package and licenses

The MSI contains `PiAoipAsio.dll`, `PiAoipControl.exe`, this guide, `PROJECT-LICENSE.txt` and `ASIO-SDK-LICENSE.txt`. It does not contain the Steinberg SDK or development test tools. Project and ASIO license terms apply separately. The package is unsigned unless its distributor signs it.
