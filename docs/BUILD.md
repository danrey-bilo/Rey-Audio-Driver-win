**English** | [Русский](BUILD.ru.md)

# Build the Windows driver

For the independent service build without ASIO and the initial ACX kernel project, see [ACX/service development](ACX-SERVICE.md). The instructions below build the existing ASIO adapter.

Requirements: Windows x64, CMake 3.20+, Ninja, LLVM-MinGW x64/UCRT and a separately obtained [Steinberg ASIO SDK](ASIO-SDK.md). The SDK is not part of this repository or its source archive.

```powershell
git clone --recurse-submodules https://github.com/danrey-bilo/Rey-Audio-Driver-win.git
cd Win11-asio-AoIP
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_CXX_COMPILER=C:/Tools/llvm-mingw/bin/x86_64-w64-mingw32-clang++.exe `
  -DCMAKE_RC_COMPILER=C:/Tools/llvm-mingw/bin/x86_64-w64-mingw32-windres.exe `
  -DASIO_SDK_DIR=C:/SDKs/asio
cmake --build build --parallel 2
```

The output is `build/bin/PiAoipAsio.dll` and `build/bin/PiAoipControl.exe`. The EXE loads the DLL beside itself. Both binaries include version 2.5.0 and embedded icons. `src/ui/assets` contains editable SVG sources and multi-resolution ICO resources. The DLL contains the dialog manifest; the EXE has its application manifest and version resource.

`external/AoIP-lib` is pinned to the compatible 2.5.0 source. Override with `-DAOIP_SOURCE_DIR=/path/to/AoIP-lib`, or use an installed `AoIP 2.5.0` SDK when no source dependency is present. AoIP-lib is a private dependency. Source builds require authorized access to its submodule or an installed compatible SDK. Public source archives do not include the private dependency.

The product build contains no automatic buffer-tuning target. Core/configuration tests, invisible UI rendering and installer packaging are maintained in the separate development workspace. User installation uses the [released MSI](INSTALL.md); an ordinary CMake build does not register the driver.

## Packaging

The 2.5.0 MSI contains the two binaries, English installation guide and project/ASIO license notices. Settings and guide shortcuts select icon resources 0 and 1 from the executable; Installed apps uses the application icon. The major-upgrade family is preserved so older packages are replaced while the per-user INI remains separate.

Do not add the Steinberg SDK headers or archive to a release. Project licensing and the external ASIO agreement are separate; see [ASIO SDK](ASIO-SDK.md).

## Validation

See [2.5.0 validation](VALIDATION.md). Building an ASIO DLL does not prove its behavior under a particular DAW workload. Validate format, buffers, stream errors and host restart behavior on the target PC. Physical converter latency requires a connected hardware backend.
