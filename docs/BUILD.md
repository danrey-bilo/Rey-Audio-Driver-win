# Build the USB-only Windows software

Use a Windows x64 C++17 toolchain with Windows SDK libraries, CMake 3.20+ and
.NET Framework 4.8 for the WPF panel/EXE installer. Obtain the external Steinberg
ASIO SDK separately; its headers are not included in this repository or package.
[ASIO SDK](ASIO-SDK.md).

Supply a [Pi5-AUSB](https://github.com/danrey-bilo/Pi5-AUSB) checkout or installed
0.1.0-compatible SDK. AoIP-lib and its former submodule are not required.
From a Windows x64 compiler environment:

```powershell
cmake -S . -B build/usb -G Ninja `
  -DPI5AUSB_SOURCE_DIR=C:/src/Pi5-AUSB `
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

Validation covers 32 contracts (IPC ownership/timeline, settings migration,
PCM alignment, mixer, endpoint mapping and single-device selection). Use the
installed COM matrix and profile tools only with an idle ASIO host, one Pi,
and unity mixer. Maximum audio test duration is 295 seconds per run.
[Current report](USB-ONLY-2.8.md). A build is not physical audio qualification.

## Release archives

After committing the tested source, package the EXE, documentation and source:

```powershell
python tools/package_usb_release.py --setup build/usb-setup --out dist/usb-release `
  --asio-notice C:/SDK/asio/LICENSE.txt --ref HEAD --version 2.8.0-preview.1
```

The tool checks the installer hash and one-device manifest, archives committed
files only, excludes external SDK headers/binaries and emits download checksums.
It neither installs nor publishes. The source archive preserves retired code
under `archive/`; this code is outside the active build.
