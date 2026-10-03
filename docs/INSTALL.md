**English** | [Русский](INSTALL.ru.md)

# Install Rey Audio Driver 2.6.1 preview

Run **Rey-Audio-Setup-2.6.1-preview.1-x64.exe**. It embeds the MSI: no ZIP
extraction, PowerShell commands or INI editing is required. A prepared PC can
also use the MSI directly. Requires Windows 11 x64 22H2+ and .NET Framework 4.8.

The own ACX kernel driver uses a **local test signature**. No purchased or
pre-existing certificate, external signer, WDK or network access is needed on
the installation PC. Self-signing MSI cannot override Windows kernel policy.
[Microsoft test-signing policy](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/the-testsigning-boot-configuration-option).

1. Start the EXE; it reads the active code-integrity policy.
2. If Secure Boot is enabled, disable it in your BIOS/UEFI manually.
3. Choose **Enable Test Mode**, review the change and approve UAC.
4. Restart Windows yourself; setup does not restart automatically.
5. Start EXE again, choose Install, approve UAC and complete MSI setup.

An already prepared PC needs only EXE/MSI, UAC and installation. Have your own
BitLocker recovery key available before changing boot policy. To return to the
normal policy, uninstall the driver before explicitly disabling Test Mode and
restoring Secure Boot. MSI changes neither BIOS nor Core Isolation.

Memory Integrity does not need to be disabled in advance. The SYS is embedded
SHA-256 signed, including for test signing with HVCI; this ACX build's HVCI
compatibility remains unqualified until loading is tested. Record an actual
device/Code Integrity error before considering a comparison with Memory Integrity
disabled. Current stand snapshot: Secure Boot off, HVCI on, Test Mode off.

Setup installs the own root device, automatic ReyAudioService, mixer/tray panel,
Start menu shortcut and a service-bound Private/LocalSubnet UDP firewall rule.
It validates SYS/INF/CAT, creates a unique certificate on that PC, signs SYS/CAT
through built-in Windows Authenticode and verifies their signatures and catalog
membership. Only the public certificate enters machine Root/TrustedPublisher.
The non-exportable temporary private key is deleted. Local signing expires after
two years; uninstall/reinstall a fresh test package to renew it. Initial MSI/EXE
are not commercially signed, so Windows can show Unknown Publisher.

Configure everything through **Mixer**, **USB**, **AoIP / LAN**. USB discovery is
automatic. Select a LAN device/address in its page. Independent profiles and
mixer settings are committed as one registry value; no product INI is installed.
Changing the local UDP port updates only the own LAN firewall rule.

Uninstall from Windows Installed apps. Only own service, root, autorun, firewall
rule and own local certificate trust are removed. Settings and public diagnostic
files remain. Driver Store staging may remain; used/foreign packages are not
forcibly removed. Package builds, MSI ICE validation, extracted payload hashes,
offline local signatures and tamper rejection passed. Elevated install/uninstall,
ACX loading, WASAPI/DAW and physical audio remain unqualified.
[Mixer and latency](MIXER.md).
