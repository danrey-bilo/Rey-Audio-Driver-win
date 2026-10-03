**English** | [Русский](INSTALL-NORMAL-WINDOWS.ru.md)

# Options without Windows Test Mode

Reviewed 2026-10-03. The current 2.7.0 preview uses a locally test-signed ACX
kernel driver and requires Test Mode. None of the alternatives below is
implemented by that installer.

| Option | Suitable use | Main limitation |
|---|---|---|
| USB Audio Class 2.0 and inbox Microsoft driver | USB audio devices without a project kernel-signing certificate | Requires a different Pi audio function and new endpoint/latency tests |
| Microsoft signing through an existing publisher | Own ACX for both LAN and USB | A willing qualified publisher is needed; acceptance is not established |
| Custom Kernel Signers | Controlled PCs with an organization-owned trust chain | Firmware enrollment and Windows reset; unsuitable for ordinary MSI installation |
| Startup option 7 / F7 | Temporary driver development | Applies only to the current boot; current Rey setup rejects this route |

## USB architecture

Windows includes `usbaudio2.sys` for compatible UAC2 devices. It supports integer
PCM up to 32 bits. Asynchronous playback requires explicit feedback; multiple
endpoints sharing a clock have limited support.
[Microsoft USB Audio 2.0](https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/usb-2-0-audio-drivers).

Proposed Rey layout: Pi UAC2 audio plus a separate control interface; the Rey
service/panel retains discovery, per-card settings and mixer controls. Calculate
the 8×8 maximum: 192000 × 8 × 4 = 6.144 MB/s in each direction, or 768 bytes
per 125 µs. This payload fits the nominal High-Speed link, but does not prove
controller scheduling, stereo-pair enumeration or sub-2-ms physical latency.
Multi-rate/format descriptors, independent clock feedback and actual WASAPI
periods need a prototype. The existing FunctionFS/WinUSB transport is not UAC2.
LAN-only virtual audio devices are not provided by this USB class driver.

## Own kernel driver

Ordinary public release signing goes through Microsoft. Hardware Dev Center
requires an account with a valid EV certificate. A partner already meeting
these requirements could submit the project; this is a proposal, not an
arranged signing service.
[Microsoft signing requirements](https://learn.microsoft.com/en-us/windows-hardware/drivers/dashboard/code-signing-reqs).

[SignPath Foundation](https://signpath.org/) offers free signing for eligible
open-source projects. Installer Authenticode signing alone does not establish
Microsoft kernel acceptance. Its published terms also require all components
to meet its open-source conditions; project eligibility and a kernel submission
path must be confirmed separately.
[Terms](https://signpath.org/terms.html).

Custom Kernel Signers can authorize a private kernel signer: Windows 11 24H2+
with the required updates, except Home; Secure Boot enabled, own PK/KEK authority,
a signed App Control policy and initial Windows reset. This is a managed-device
deployment with recovery planning.
[Microsoft CKS requirements](https://learn.microsoft.com/en-us/windows/security/application-security/application-control/app-control-for-business/design/custom-kernel-signers).

F7 disables signature enforcement for one boot only. A connected kernel debugger
is another development option, not an ordinary installation process.
[Temporary testing mechanisms](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/installing-an-unsigned-driver-during-development-and-test).

Adding a local certificate to Root/TrustedPublisher or disabling Secure Boot
or Memory Integrity alone is not a production signature for the new ACX driver.
The existing setup therefore does not silently relax these checks. A previously
signed third-party virtual driver is an alternative only if its license and
unmodified interface permit the required channels, capture/playback and timing.

Installing the panel/service normally and adding the driver through Device
Manager is possible as a packaging design. The manual file is INF, alongside
SYS and CAT. It changes installation steps, not kernel trust: Windows may
reject the package or leave the device with Code 52. Current Rey setup installs
both components together and requires Test Mode.
[Microsoft installation/loading distinction](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/driver-signing)
· [Code 52](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/cm-prob-unsigned-driver).

For a simple free USB installation, prototype UAC2 first and compare measured
latency with the present transport. For own LAN endpoints, use a qualified
signing partner or deliberately provision controlled PCs; the current preview
remains available for explicit Test Mode development.

[Current installation](INSTALL.md) · [Windows channel pairs](ENDPOINTS.md)
