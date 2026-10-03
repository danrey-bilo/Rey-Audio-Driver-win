[English](README.md) | **Русский**

# LAN-измерения и проверки релиза

Свежие файлы: 300 секунд, block256/guard1536/callback50 — **PASS**. Цифровой RTT p50/p95/p99/max — **8.920/9.446/9.573/16.599 мс**, 1,123,192 samples. Основные Windows errors: ноль.

SHA-256 службы: `9b80dc78adc192cba79acbdfd6bb4a6ef7ba341cfe5862197782829bd1b38f63`; Pi5: `252a146bad247a75332b6710a52b04bd0a7360c3bae1fd47163d3e380a2aad1c`.

Callback/wake/RX gap/TX age max: 161.6/692.9/7270.0/884.0 мкс. Pi process CPU: 7.935% одного ядра. Полные CPU/counters/interface/throttle snapshots сохранены в JSON. Снимки питания до/после не являются непрерывным контролем.

Возврат исходных binary/config, LAN/Wi-Fi и CPU isolation независимо проверен. Hash peer в runtime DEB совпадает с проверенной нативной сборкой.

Предыдущий свежий block256/guard1024 выполнялся 900 с и **НЕ ПРОШЁЛ**: late/missing=1568/1568, RX gap max=7427.9 мкс. Низкая медиана и прежнее чистое окно не подтверждают надёжный минимум. Больший guard проверяется отдельным ручным профилем; wire budget тот же. Все последующие прогоны по решению владельца ограничены 300 секундами.

[JSON свежего прогона](evidence/release-2.5.0-lan-margin-5min/release-b256-g1536-cb50.json) · [Восстановление](evidence/release-2.5.0-lan-margin-5min/restoration-verified.json)

Прежний поиск выполнен на файлах с другими hashes: block256/guard1024 прошёл 900 секунд с RTT 6,251/6,755/6,891/11,718 мс. Кандидат 128/448 не прошёл: 5312 late/missing frames и один host overrun. Короткое чистое окно не подтверждает устойчивость меньшего guard. Ниже сохранены удачные и неудачные серии.

- [AOIP_LAN_15MIN_VALIDATION_2026-10-03_RU](AOIP_LAN_15MIN_VALIDATION_2026-10-03_RU.md)
- [AOIP_LAN_MARGIN_VALIDATION_2026-10-03_RU](AOIP_LAN_MARGIN_VALIDATION_2026-10-03_RU.md)
- [AOIP_LAN_MINIMUM_LATENCY_2026-10-03_RU](AOIP_LAN_MINIMUM_LATENCY_2026-10-03_RU.md)
- [AOIP_LAN_STARTUP_FIX_2026-10-03_RU](AOIP_LAN_STARTUP_FIX_2026-10-03_RU.md)

Измерен synthetic digital Pi → Windows service echo → Pi. ADC/DAC, реальная DAW, установленный ACX/WASAPI, поддерживаемая MSVC-сборка драйвера и fault recovery guard1024 ещё не квалифицированы. Buffers ручные. Несколько Pi отложены. Для USB будет отдельный репозиторий/библиотека; реализация сейчас отложена. Pi4 не пересобран/не проверен и не изменён. **Pre-release.**

При переносе заменены только абсолютные пути workspace. Числа и hashes binaries сохранены; [export provenance](export-manifest.json) содержит исходные и экспортированные hashes. Локальные первичные данные не перезаписывались.
