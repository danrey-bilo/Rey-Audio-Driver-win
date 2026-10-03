**English** | [Русский](ARCHITECTURE.ru.md)

# Windows driver architecture

```mermaid
flowchart LR
  PI[Raspberry Pi / UDP] --> RX[Receive worker]
  RX --> Q[Bounded SPSC queue]
  Q --> T[Timeline + manual LAN buffer]
  T --> A[Audio worker / ASIO callbacks]
  A --> TX[Transmit worker]
  TX --> PI
  UI[Settings and discovery] --> CONFIG[Per-user profile]
  CONFIG --> A
```

The driver is an in-process x64 ASIO DLL. Audio threads use MMCSS and bounded work. The separate settings/tray executable loads the same panel from the DLL. Networking uses ordinary Windows UDP sockets; there is no custom NIC driver or kernel-bypass layer.

The receive worker validates packets and queues them. The audio worker owns the frame timeline, fills ASIO inputs and advances callbacks. The transmit worker sends ready output blocks with MTU fragmentation and bounded expiry/retry behavior. Windows scheduling and driver delays still affect the path.

ASIO block size and LAN receive guard are separate manual controls. Automatic tuning was removed. Device capabilities are discovered; no particular PC, NIC model or IPv4 address is built into the driver. Optional per-thread CPU overrides are advanced configuration, not default universal settings.

V3 adds masks, a leased session and exact digital-zero suppression. Physical channel counts remain device-owned. Legacy firmware uses V1/V2 behavior. One streaming ASIO client is supported. The driver does not provide Windows shared-mode endpoints or a hardware ADC/DAC backend.

See [host API](API.md), [buffer guide](BUFFER-GUIDE.md) and [validation](VALIDATION.md).


## Service/driver path in 2.5

```mermaid
flowchart LR
    Pi["Pi5 CPU0: synthetic PCM peer"]
    Net["Built-in LAN / UDP"]
    RX["Windows IOCP"]
    Engine["SessionEngine: slots / Timeline / frame clock"]
    Bridge["Bounded PCM bridge"]
    ACX["ACX child: capture + render circuits"]
    OS["WASAPI / Windows audio clients"]
    Pi <--> Net
    Net <--> RX
    RX <--> Engine
    Engine <--> Bridge
    Bridge <--> ACX
    ACX <--> OS
```

StartGate prepares workers and publishes the accepted epoch before PCM is released. DeviceId and the V3 lease bind one service owner to one physical Pi. The diagram describes the implemented development layers; the ACX/WASAPI path has not been installed or measured. ASIO remains a separate adapter using shared transport components.

The development driver creates one multichannel input endpoint and one multichannel output endpoint (up to eight channels each). Multiple Pi devices and independent channel endpoints require separate clock, enumeration and lifecycle work.
