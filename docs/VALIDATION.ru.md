[English](VALIDATION.md) | **Русский**

# Validation 2.5.0

| Проверка | Результат / область |
|---|---|
| Windows x64 Release | 13/13 CTest: core, transport, datagrams, leases, budget, PCM, examples, config, IOCP, bridge, session, StartGate |
| Самостоятельная служба Windows | ASIO отключён, SDK не задан; сборка service/probe прошла |
| Pi5 ARM64 Release | 9/9 native CTest with RT policy; GCC14, Debian13, PREEMPT_RT 6.18.50 |
| Установленные SDK exports | Три consumers прошли: Windows AoIP, ARM64 AoIP, ARM64 Pi5; точная версия 2.5.0 |
| ACX x64 | Clang22 MSVC ABI build, InfVerif and Inf2Cat passed; не подписан / не установлен |
| Пакеты | Проверены MSI tables/payload/versions и ARM64 DEB metadata/content; нового цикла установки не было |

Свежие файлы: 300 секунд, block256/guard1536/callback50 — **PASS**. Цифровой RTT p50/p95/p99/max — **8.920/9.446/9.573/16.599 мс**, 1,123,192 samples. Основные Windows errors: ноль.

SHA-256 службы: `9b80dc78adc192cba79acbdfd6bb4a6ef7ba341cfe5862197782829bd1b38f63`; Pi5: `252a146bad247a75332b6710a52b04bd0a7360c3bae1fd47163d3e380a2aad1c`.

Callback/wake/RX gap/TX age max: 161.6/692.9/7270.0/884.0 мкс. Pi process CPU: 7.935% одного ядра. Полные CPU/counters/interface/throttle snapshots сохранены в JSON. Снимки питания до/после не являются непрерывным контролем.

Возврат исходных binary/config, LAN/Wi-Fi и CPU isolation независимо проверен. Hash peer в runtime DEB совпадает с проверенной нативной сборкой.

Предыдущий свежий block256/guard1024 выполнялся 900 с и **НЕ ПРОШЁЛ**: late/missing=1568/1568, RX gap max=7427.9 мкс. Низкая медиана и прежнее чистое окно не подтверждают надёжный минимум. Больший guard проверяется отдельным ручным профилем; wire budget тот же. Все последующие прогоны по решению владельца ограничены 300 секундами.

Измерен synthetic digital Pi → Windows service echo → Pi. ADC/DAC, реальная DAW, установленный ACX/WASAPI, поддерживаемая MSVC-сборка драйвера и fault recovery guard1024 ещё не квалифицированы. Buffers ручные. Несколько Pi отложены. Для USB будет отдельный репозиторий/библиотека; реализация сейчас отложена. Pi4 не пересобран/не проверен и не изменён. **Pre-release.**

[Полные измерения и неудачные прогоны](https://github.com/danrey-bilo/Win11-asio-AoIP/blob/v2.5.0/docs/measurements/README.ru.md) · [Описание релиза](RELEASE-2.5.0.ru.md)
