# Pi5-AUSB transport

Rey Audio Driver 2.7.0 uses the independent
[Pi5-AUSB](https://github.com/danrey-bilo/Pi5-AUSB) library. Build with
`PIAOIP_BUILD_ASIO=OFF`, `PIAOIP_ENABLE_USB=ON` and `PI5AUSB_SOURCE_DIR` pointing
to that repository, or supply its matching 0.1.0 SDK. Generic development
builds may keep USB disabled; the Rey release includes the adapter.

## Installed application

Use [EXE/MSI](INSTALL.md). Supported USB interfaces are discovered automatically;
the service verifies their permanent DeviceId and opens their exact paths.
Each board has its own session and saved USB/LAN/mixer profiles. The same ID
over USB and LAN remains one board, with one active route. Configured LAN is
the default; choose **Use USB** on that card's USB page to change its route.
The card selector changes the view only. No INI is installed.

The service retries USB discovery/connection after removal. Format or route
changes restart only that board's session. Live mixer edits do not restart it.
Discovery and a bounded reconnect passed finite checks on one real Pi; sleep,
resume, USB resets and loaded kernel lifecycle still need qualification.
[Cards and endpoints](ENDPOINTS.md) · [2.7.0 evidence](REY-AUDIO-2.7.md).

## Digital diagnostics

The legacy headless tool remains available for finite transport-only checks:

```powershell
PiAoipService.exe --console --echo --transport usb --seconds 5 --usb-depth 3
```

This echo test does not install a Windows sound device or test converters.
The Pi enumerated at High-Speed 480 Mb/s using inbox WinUSB; FunctionFS/AIO
transport checks covered all 18 formats, 8×8/PCM16/24/32, six rates 44.1–192 kHz.
The earlier 299s 192000/32-bit/depth3 run measured digital RTT p50/p95/p99/max
373/410/419/944.2 µs, without corrupt PCM or USB errors.
[Exact USB builds/results](https://github.com/danrey-bilo/Pi5-AUSB/blob/v0.1.0/docs/RESULTS.md).

The present user-mode adapter exchanges PCM with ACX through bounded IOCTLs.
Physical audio below 2 ms needs loaded endpoints, measured WASAPI periods,
device-clock feedback and ADC/DAC loopback. A direct KMDF USB path is a future
experiment. USB UAC2 with the Microsoft class driver is a separate proposal;
it is not this protocol. [Installation alternatives](INSTALL-NORMAL-WINDOWS.md).

## Русский

Релиз Rey Audio 2.7.0 включает самостоятельную библиотеку Pi5-AUSB без ASIO SDK.
Пользователь устанавливает EXE/MSI и настраивает USB в панели, без INI. Служба
сама обнаруживает USB, проверяет DeviceId и открывает конкретный путь. Профили
и микшер отдельные для каждой карты; выбор сверху меняет только отображение.
Один ID по USB/LAN объединяется в одну карту. После настройки LAN он выбран по
умолчанию; **Использовать USB** явно переключает транспорт этой карты.

Проверены цифровые 18 форматов 8×8, обнаружение и ограниченное по времени
переподключение одного реального Pi. Старый 299-секундный тест показал RTT
373/410/419/944,2 мкс; это транспорт, без системного звука Windows и ADC/DAC.
Сон, resume, reset, загрузка ACX и физический loopback ещё требуют проверки.
UAC2 со встроенным драйвером Microsoft — отдельный предложенный режим.
[Результаты 2.7.0](REY-AUDIO-2.7.ru.md) · [Варианты без Test Mode](INSTALL-NORMAL-WINDOWS.ru.md).
