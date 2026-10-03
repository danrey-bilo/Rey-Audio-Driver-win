# Historical Windows endpoint installer

The 2.7 ACX MSI/bootstrap sources and their build/package helpers are preserved
here for reference. They are excluded from the 2.8 USB-ASIO build and installer.
This archive is not a supported build entry point. Use Git history for the
complete matching source tree and old release documentation.

The current installer is built with `tools/build_usb_asio_setup.ps1` at the
repository root. It installs one-device USB-ASIO, the automatic service and the
mixer/tray panel. It contains no new kernel driver or test certificate and does
not alter Windows boot or security settings. Ordinary Windows audio endpoints
remain a separate qualification stage.
