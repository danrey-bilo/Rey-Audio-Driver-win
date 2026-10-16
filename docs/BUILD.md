# Build the USB-only Windows software

Use a Windows x64 C++17 toolchain with Windows SDK libraries, CMake 3.20+ and
.NET Framework 4.8 for the WPF panel/EXE installer. Obtain the external Steinberg
ASIO SDK separately; its headers are not included in this repository or package.
[ASIO SDK](ASIO-SDK.md).

Initialize the pinned [Pi5-AUSB](https://github.com/danrey-bilo/Pi5-AUSB) 0.3
checkout at `lib/pi5-ausb` (repository access required), or supply an installed
0.3 package. AoIP-lib is not required.
From a Windows x64 compiler environment:

```powershell
cmake -S . -B build/usb -G Ninja `
  -DASIO_SDK_DIR=C:/SDK/asio `
  -DCMAKE_BUILD_TYPE=Release
cmake --build build/usb
ctest --test-dir build/usb --output-on-failure
powershell -NoProfile -ExecutionPolicy Bypass -File tools/build_usb_asio_setup.ps1 `
  -Bin build/usb/bin -Output build/usb-setup
```

The EXE is `build/usb-setup/Rey-Audio-USB-ASIO-Setup-x64.exe`. `SHA256.json`
records the four embedded binaries and installer hash. No INI, SYS/CAT,
SDK headers or network firewall configuration enters the USB installer.
Verify an installed package from x64 PowerShell using
`tools/verify_usb_install.ps1 -SetupDirectory build/usb-setup -Output build/install-check.json`.

`REY_BUILD_USB_ASIO`, `REY_BUILD_CONTROL` and `REY_BUILD_TESTS` default to ON.
Set ASIO OFF for a service-only development build. The TAG endpoint experiment
is separately opt-in with `REY_ENABLE_TAG_BRIDGE=ON` and is not in the delivered
ASIO package. ACX development tooling is separate from this supported EXE path.

Minimal LLVM-MinGW environments can supply `REY_SETUPAPI_LIBRARY`,
`PI5AUSB_SETUPAPI_LIBRARY`, `PI5AUSB_WINUSB_LIBRARY`, `REY_WASAPI_HEADERS`,
`CMAKE_CXX_COMPILER` and `CMAKE_RC_COMPILER`. Standard SDK toolchains use their
own import libraries. ARM64 is not supported.

Validation covers 39 contracts (capture publication, IPC ownership/timeline, bounded render waits, settings migration,
PCM alignment, mixer, endpoint mapping and single-device selection). Use the
installed COM matrix and profile tools only with an idle ASIO host, one Pi,
and unity mixer. Audio tests use at most 175 seconds, completing within three minutes.
[Current report](USB-LATENCY-2026-10-16.md). A build is not physical audio qualification.

The AKM mode-control development also has 23 C# contracts, independent of the
external ASIO SDK: `./tools/check_board_control.ps1`. Run
`./tools/build_control.ps1` for the WPF panel. See [AKM modes](AKM-OPERATING-MODES.md)
for the coordinated Pi USB/JACK integration and its qualification boundary.

## Release archives

After committing the tested source, package the EXE, documentation and source:

```powershell
python tools/package_usb_release.py --setup build/usb-setup --out dist/usb-release `
  --asio-notice C:/SDK/asio/LICENSE.txt --ref HEAD --version 2.8.2-akm-dev.5
```

The tool checks the installer hash and one-device manifest, archives committed
files only, excludes external SDK headers/binaries and emits download checksums.
It neither installs nor publishes. Initialize the pinned transport separately
when rebuilding the source archive; its private source is not exported by the
Windows packager. `REY_FINITE_QUALIFICATION=ON` creates an isolated test build
that cannot register ASIO or be admitted to the installer.
