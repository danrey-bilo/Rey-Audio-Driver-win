# Synchronous Audio Router 0.13.1

Original project: <https://github.com/eiz/SynchronousAudioRouter>.
Exact source: tag `v0.13.1`, commit
`c521ce48ef99091bb1d031b82e04b6b9f14e2854`, retained in `upstream-source.zip`.
The public `sar.h` interface, upstream GPL notices and corresponding source are
preserved. `provenance.json` records the original download and SHA256 hashes.

`runtime/` contains the **unchanged signed** SYS, INF and CAT extracted from the
official x64 release. The Rey installer embeds those three files, notices and
the corresponding source. Packaging rejects changed runtime/source hashes.
No SAR ASIO wrapper, routing application, application-injection DLL or network
filter is included or registered.

Rey uses the driver's public IOCTL and shared-memory protocol directly, through
`src/bridge/sar_bridge.cpp`. The installer owns only
`ROOT\REYAUDIO_SAR\0000`, prepares the third-party Windows `EP` property stores
and refuses to take over another SAR root. Signed files are never patched.

The 2018 release uses an old cross-signature. Loading was verified on the bench
Windows 11 build 26300, where Secure Boot and Memory Integrity were already off.
This does not establish compatibility with newer security policies or other PCs.
The installer never changes boot/signature/security policy. Installation is a
development candidate; see [Windows endpoint qualification](../../docs/ENDPOINTS.md).
