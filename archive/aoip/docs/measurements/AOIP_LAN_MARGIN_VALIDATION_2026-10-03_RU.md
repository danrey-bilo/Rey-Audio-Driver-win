# Проверка LAN одного Pi с увеличенным guard

Основной 900-секундный цифровой прогон: **FAIL**. Возврат исходной службы Pi и сети: **проверено**. Часовые тесты отменены по запросу пользователя.

Проверяется synthetic Pi → Windows service echo → Pi через встроенные Ethernet-порты. Это цифровой RTT, не задержка ADC/DAC и не подтверждение ACX/WASAPI или реальной DAW. Учитывается заданное пользователем ограничение Windows/LAN 1,2 мс; приоритеты Windows и настройки NIC не менялись.

Основной профиль: 8×8 каналов, 192000 Гц, PCM24, capture=32, block=64, guard=1536, EnergySaving=0. Основной прогон имеет искусственную нагрузку callback 50 мкс. Короткие проверки reopen/recovery используют callback 50 мкс.

Окно сценария UTC: 2026-10-02T22:53:46.809387+00:00 — 2026-10-02T23:09:48.724278+00:00. Заданный суммарный PCM streaming в завершённых обычных прогонах: 930 секунд. Это отдельные открытия сессии, а не непрерывное суммарное аудиоокно. [Полный suite.json](evidence/lan-margin-20261003/suite.json).

## Цифровой RTT

Время — мс; seconds задаёт длительность fixture, elapsed включает startup и сбор counters. RTT probe включается примерно спустя 0,5 с. Число RTT samples относится к маркерам, а не ко всем PCM frames. PASS требует нулевых error counters Windows, Pi и интерфейса; наличие перестановок пакетов само по себе не считается потерей.

| Прогон | Seconds | Elapsed | Block/guard | Callback us | RTT samples | p50 / p95 / p99 / max ms | Итог |
| --- | --- | --- | --- | --- | --- | --- | --- |
| [margin-b64-g1536-cb50](evidence/lan-margin-20261003/margin-b64-g1536-cb50.json) | 900 | 900.27 | 64/1536 | 50 | 5397210 | 8.392 / 8.532 / 8.567 / 13.446 | FAIL |
| [crash-owner-recovery](evidence/lan-margin-20261003/crash-owner-recovery.json) | 15 | 15.27 | 64/512 | 50 | 87056 | 3.059 / 3.198 / 3.222 / 4.091 | PASS |
| [admin-link-recovery](evidence/lan-margin-20261003/admin-link-recovery.json) | 15 | 15.27 | 64/512 | 50 | 86927 | 3.062 / 3.209 / 3.313 / 14.974 | FAIL |

## Ошибки сроков и очередей Windows

| Прогон | late/missing frames | deadline/skipped frames | overrun/expired output frames | invalid/RX overflow | TX overflow/expired/errors |
| --- | --- | --- | --- | --- | --- |
| margin-b64-g1536-cb50 | 0/0 | 0/0 | 1/0 | 0/0 | 0/0/0 |
| crash-owner-recovery | 0/0 | 0/0 | 0/0 | 0/0 | 0/0/0 |
| admin-link-recovery | 3488/3488 | 0/0 | 0/0 | 0/0 | 0/9/0 |

Связанные late/missing/skipped counters нельзя складывать как уникальные потери.

| Прогон | control/MMCSS/resync | IO errors/overflows | Pi lost/bad/skipped/duplicate/expired | Pi reorder/retries |
| --- | --- | --- | --- | --- |
| margin-b64-g1536-cb50 | 0/0/0 | 0/0 | 0/0/0/0/0 | 823/0 |
| crash-owner-recovery | 0/0/0 | 0/0 | 0/0/0/0/0 | 11/0 |
| admin-link-recovery | 0/0/0 | 0/0 | 9/0/0/0/0 | 267/0 |

## CPU и максимальные интервалы

| Прогон | callback/wake/RX gap/TX age max us | Win audio/RX/TX % | Pi process % | Pi CPU window s |
| --- | --- | --- | --- | --- |
| margin-b64-g1536-cb50 | 950.9/1352.6/5211.2/355.7 | 22.722/3.085/3.691 | 8.315 | 900.63 |
| crash-owner-recovery | 64.9/1002.1/1187.1/160.3 | 22.911/2.916/4.374 | 8.005 | 15.53 |
| admin-link-recovery | 104.9/791.3/11872.8/3018.3 | 23.022/3.854/3.542 | 8.184 | 15.42 |

CPU указан в процентах одного логического процессора Windows или одного ядра Pi. Pi process CPU вычисляется по schedstat потоков. Свободные ядра 2/3 сохранены. Max wake/RX gap/TX age описывает хвосты наблюдаемого окна, а не каждую audio callback.

| Прогон | eth0 RX/TX errors/drops | Pi RX/TX counter Mbps | Throttle |
| --- | --- | --- | --- |
| margin-b64-g1536-cb50 | 0/0/0/0 | 40.486/41.159 | throttled=0x50000 → throttled=0x50000 |
| crash-owner-recovery | 0/0/0/0 | 39.137/39.797 | throttled=0x50000 → throttled=0x50000 |
| admin-link-recovery | 0/0/0/0 | 39.399/40.063 | throttled=0x50000 → throttled=0x50000 |

Счётчики eth0 bytes не имеют того же overhead, что расчёт полного wire budget. Одинаковое историческое throttled слово не исключает повторного события, чей history bit уже был выставлен. Эти снимки не заменяют измерение питания во время USB streaming.

## Владение устройством и восстановление

| Действующий владелец | Exit второго процесса | BUSY получен |
| --- | --- | --- |
| margin-b64-g1536-cb50 | 5 | True |

| Проверка | Наблюдение | Механизм | Recovery PCM |
| --- | --- | --- | --- |
| [crash-owner](evidence/lan-margin-20261003/crash-owner.json) | Убит созданный fixture процесс; immediate owner exit=5, BUSY=True; новое открытие после ожидания истечения 3-секундной lease | PASS | PASS |
| [admin-link-cycle](evidence/lan-margin-20261003/admin-link-cycle.json) | eth0 административно down на 10 с; Windows console owner exit=6; eth0 возвращён по timer и проверен через Wi-Fi SSH | PASS | FAIL |

В этой серии отдельный цикл graceful reopen не повторяется; его результаты сохранены в отчёте startup fix. Crash test завершает только процесс, созданный fixture. Link test использует административное выключение eth0; физическое извлечение кабеля этим не проверялось. После потери соединения запускается новый console owner: автоматический reconnect установленной SCM-службы здесь не проверен. Fault counters сохраняются отдельно и не выдаются за clean steady-state окно.

## Binary и возврат стенда

Windows service SHA256: `9fe74f024a9bffb4fe427af261b3264dad60cc191c96af0559cc15d8708233bc`.

Испытанный Pi peer SHA256: `d54ef6ad5e04829119a51c143669b582965b966b0a281016a9113863067565aa`.

Перед тестом сняты hash executable, command и hashes конфигурации штатной службы. Кандидат запускается временным unit без замены установленного peer. 30-минутный watchdog возвращает службу при потере координатора; link timer поднимает eth0 через 10 с. После завершения исходный executable/command/config сравниваются со снимком.

[До теста](evidence/lan-margin-20261003/original.json) · [После восстановления](evidence/lan-margin-20261003/restored.json).

Первый сценарий, предусматривавший часовые проверки, остановлен по уточнению пользователя через несколько минут. Его незавершённый guard448 прогон не квалифицирован и не входит в таблицы PASS/FAIL или длительность этой серии. Данные и подтверждение возврата стенда сохранены отдельно в соседнем каталоге lan-qualification-20261003.

## Что остаётся для полного аудиотракта

Загрузка unsigned ACX драйвера на Windows-стенде, реальные WASAPI/KS periods и duplex ещё не проверены. ADC/DAC capture backend на Pi не подключён. Для реальной задержки нужны системный драйвер, подключённый audio backend и физический loopback. Все последующие проверочные прогоны ограничены 15 минутами по запросу пользователя. Несколько Pi остаются отложены. Возможности RIO/WSK/NDIS не являются условием завершения нынешней цифровой LAN-проверки.

Дополнительный read-only postflight после завершения suite: исходные binary/command/config и CPU isolation, eth0 UP/IPv4, сохранённый Wi-Fi, неактивные candidate unit и оба restoration timers. [Проверка возврата стенда](evidence/lan-margin-20261003/restoration-verified.json).

## Цена увеличенного guard

Guard выбран вручную: 1536 frames / 192000 Гц = **8 мс** против 512 frames = **2,667 мс**. Дополнительный запас составляет 5,333 мс; transport packet size, частота callbacks и wire traffic не увеличиваются. Это проверка конкретного запаса буфера, а не снижение задержки Windows worker.

Предшествующий 300-секундный тест того же service binary с guard512 имел 1440 late/missing frames и RX gap 7,522 мс. [Startup fix и исходные counters](AOIP_LAN_STARTUP_FIX_2026-10-03_RU.md). Текущий guard1536 прогон имеет overrun1 и остаётся FAIL; guard512 также не прошёл свою основную проверку. Короткие crash/link recovery в таблице используют guard512; их следует оценивать отдельно от основного guard1536 окна.

Это не поиск математически минимального guard и не строгое A/B в одном временном окне. Guard512/300 с и guard1536/900 с используют один новый binary и callback50, но имеют разную длительность и моменты запуска. Старый 900-секундный FAIL использовал callback100 и прежнюю сборку. Новый тест не квалифицирует callback100 или реальную DAW.

Изменения приоритета Windows, NIC или частоты CPU не выполнялись. Текущие throttle flags проверяются в снимках до/после; если низшие биты слова ненулевые хотя бы в одном снимке, полная qualification не может быть PASS. Между снимками питание непрерывно не измерялось.
