# Optional Pi5-AUSB transport

The USB development adapter is independent of ASIO. Build with
`PIAOIP_BUILD_ASIO=OFF`, `PIAOIP_ENABLE_USB=ON`, and `PI5AUSB_SOURCE_DIR` pointing
to [Pi5-AUSB](https://github.com/danrey-bilo/Pi5-AUSB), or provide its exact-version
0.1.0 SDK. Default builds keep USB disabled and retain LAN.

```powershell
PiAoipService.exe --console --echo --transport usb --seconds 5 --usb-depth 3
```

Use [service.usb.example.ini](../config/service.usb.example.ini) with `--config`
to set rate/bits. `[Service] Transport=lan|usb` selects the route on service start;
stop before changing it. `DeviceId` optionally selects an exact USB identity. The
same profile/identity attaches to the own ACX bridge.

The Pi5 enumerated at High-Speed 480 Mb/s with inbox WinUSB, without signing/
security changes. FunctionFS/AIO echoes exact PCM across eight channels. All 18
formats and the service adapter passed digital tests. A 299-second 192000/32-bit
test at queue3 recorded RTT p50/p95/p99/max 373/410/419/944.2 us without corrupt
PCM or USB errors. This is digital USB RTT; no installed sound endpoint or ADC/DAC
latency is established.

The development service uses user-mode PCM and synchronous bridge IOCTLs.
Production work: KMDF completions into bounded ACX rings, shared LAN/USB hardware
lease and actual sample-clock feedback. The USB service stops on disconnect;
hotplug and sleep/resume require further work. Stable LAN tags are retained.

## Русский

USB-адаптер включается через `PIAOIP_ENABLE_USB=ON`, ASIO SDK не нужен. LAN выбран
по умолчанию. USB: `--transport usb` или `[Service] Transport=usb`, смена после
остановки. Один DeviceId и профиль используются для LAN/USB и ACX bridge.

Проверены USB High-Speed, 18 форматов и цифровой PCM-адаптер. За 299 секунд:
RTT p50/p95/p99/max 0,373/0,410/0,419/0,9442 мс, ошибок PCM/USB не обнаружено.
Установка звуковых endpoints, ADC/DAC, аппаратный clock/lease, hotplug и прямой
путь USB KMDF → ACX требуют отдельного завершения.
