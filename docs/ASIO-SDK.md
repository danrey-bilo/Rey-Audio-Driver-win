**English** | [Русский](ASIO-SDK.ru.md)

# External Steinberg ASIO SDK

The driver implements its own ASIO code using separately obtained Steinberg interface headers. The repository and full source archive do not contain the SDK. Obtain it from [Steinberg Developers](https://www.steinberg.net/developers/) and set `ASIO_SDK_DIR` when building.

| Header | Use |
|---|---|
| `common/asio.h` | ASIO types, callbacks, sample formats and errors |
| `common/iasiodrv.h` | Driver/host `IASIO` interface |

The local SDK license offers the proprietary Steinberg ASIO license or GPLv3. This project's personal/noncommercial and separately licensed commercial terms are not GPLv3. They do not replace or grant rights under the external SDK license.

The proprietary SDK notice requires a Steinberg-signed agreement before publishing software under that license. SDK redistribution also requires its own permission. Distributors are responsible for satisfying both sets of terms and retaining the required notices. No SDK files are bundled as a developer kit in the MSI or source release.

The Windows package includes the SDK license notice alongside the project license. ASIO is a trademark and software of Steinberg Media Technologies GmbH.
