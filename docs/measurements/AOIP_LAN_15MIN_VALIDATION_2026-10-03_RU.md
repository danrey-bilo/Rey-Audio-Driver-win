# Проверка LAN одного Pi за 15 минут

Основной 900-секундный цифровой прогон: **FAIL**. Возврат исходной службы Pi и сети: **проверено**. Часовые тесты отменены по запросу пользователя.

Проверяется synthetic Pi → Windows service echo → Pi через встроенные Ethernet-порты. Это цифровой RTT, не задержка ADC/DAC и не подтверждение ACX/WASAPI или реальной DAW. Учитывается заданное пользователем ограничение Windows/LAN 1,2 мс; приоритеты Windows и настройки NIC не менялись.

Основной профиль: 8×8 каналов, 192000 Гц, PCM24, capture=32, block=64, guard=512, EnergySaving=0. Основной прогон имеет искусственную нагрузку callback 100 мкс. Короткие проверки reopen/recovery используют callback 50 мкс.

Окно сценария UTC: 2026-10-02T22:06:48.521264+00:00 — 2026-10-02T22:23:21.908400+00:00. Заданный суммарный PCM streaming в завершённых обычных прогонах: 960 секунд. Это отдельные открытия сессии, а не непрерывное суммарное аудиоокно. [Полный suite.json](evidence/lan-qualification-20261003-15min/suite.json).

## Цифровой RTT

Время — мс; seconds задаёт длительность fixture, elapsed включает startup и сбор counters. RTT probe включается примерно спустя 0,5 с. Число RTT samples относится к маркерам, а не ко всем PCM frames. PASS требует нулевых error counters Windows, Pi и интерфейса; наличие перестановок пакетов само по себе не считается потерей.

| Прогон | Seconds | Elapsed | Block/guard | Callback us | RTT samples | p50 / p95 / p99 / max ms | Итог |
| --- | --- | --- | --- | --- | --- | --- | --- |
| [15min-b64-g512-cb100](evidence/lan-qualification-20261003-15min/15min-b64-g512-cb100.json) | 900 | 900.27 | 64/512 | 100 | 5397198 | 3.053 / 3.093 / 3.203 / 4.763 | FAIL |
| [graceful-reopen-0](evidence/lan-qualification-20261003-15min/graceful-reopen-0.json) | 10 | 10.26 | 64/512 | 50 | 57024 | 2.995 / 3.028 / 3.160 / 3.236 | FAIL |
| [graceful-reopen-1](evidence/lan-qualification-20261003-15min/graceful-reopen-1.json) | 10 | 10.27 | 64/512 | 50 | 57047 | 3.016 / 3.157 / 3.183 / 3.369 | PASS |
| [graceful-reopen-2](evidence/lan-qualification-20261003-15min/graceful-reopen-2.json) | 10 | 10.27 | 64/512 | 50 | 56998 | 3.016 / 3.156 / 3.183 / 6.140 | FAIL |
| [crash-owner-recovery](evidence/lan-qualification-20261003-15min/crash-owner-recovery.json) | 15 | 15.28 | 64/512 | 50 | 87113 | 3.016 / 3.152 / 3.178 / 3.355 | PASS |
| [admin-link-recovery](evidence/lan-qualification-20261003-15min/admin-link-recovery.json) | 15 | 15.27 | 64/512 | 50 | 87099 | 3.057 / 3.199 / 3.214 / 4.980 | PASS |

## Ошибки сроков и очередей Windows

| Прогон | late/missing frames | deadline/skipped frames | overrun/expired output frames | invalid/RX overflow | TX overflow/expired/errors |
| --- | --- | --- | --- | --- | --- |
| 15min-b64-g512-cb100 | 0/0 | 0/0 | 1/0 | 0/0 | 0/0/0 |
| graceful-reopen-0 | 0/0 | 0/1152 | 0/0 | 0/0 | 0/0/0 |
| graceful-reopen-1 | 0/0 | 0/0 | 0/0 | 0/0 | 0/0/0 |
| graceful-reopen-2 | 0/0 | 0/0 | 0/0 | 0/0 | 0/90/0 |
| crash-owner-recovery | 0/0 | 0/0 | 0/0 | 0/0 | 0/0/0 |
| admin-link-recovery | 0/0 | 0/0 | 0/0 | 0/0 | 0/0/0 |

Связанные late/missing/skipped counters нельзя складывать как уникальные потери.

| Прогон | control/MMCSS/resync | IO errors/overflows | Pi lost/bad/skipped/duplicate/expired | Pi reorder/retries |
| --- | --- | --- | --- | --- |
| 15min-b64-g512-cb100 | 0/0/0 | 0/0 | 0/0/0/0/0 | 866/0 |
| graceful-reopen-0 | 0/0/1 | 0/0 | 0/0/0/0/0 | 0/0 |
| graceful-reopen-1 | 0/0/0 | 0/0 | 0/0/0/0/0 | 7/0 |
| graceful-reopen-2 | 0/0/0 | 0/0 | 90/0/0/0/0 | 265/0 |
| crash-owner-recovery | 0/0/0 | 0/0 | 0/0/0/0/0 | 10/0 |
| admin-link-recovery | 0/0/0 | 0/0 | 0/0/0/0/0 | 14/0 |

## CPU и максимальные интервалы

| Прогон | callback/wake/RX gap/TX age max us | Win audio/RX/TX % | Pi process % | Pi CPU window s |
| --- | --- | --- | --- | --- |
| 15min-b64-g512-cb100 | 885.9/1428.8/1094.0/346.9 | 38.819/2.917/3.384 | 8.939 | 900.63 |
| graceful-reopen-0 | 75.5/179.1/353.5/133.3 | 22.506/2.813/2.970 | 8.067 | 10.42 |
| graceful-reopen-1 | 72.8/258.7/357.8/243.9 | 23.587/3.436/4.374 | 8.216 | 10.44 |
| graceful-reopen-2 | 111.3/259.5/409.6/17976.8 | 23.258/3.590/4.527 | 8.246 | 10.50 |
| crash-owner-recovery | 136.1/210.4/336.1/135.6 | 21.856/3.122/3.434 | 8.348 | 15.40 |
| admin-link-recovery | 133.0/760.5/2034.3/429.6 | 24.045/2.498/2.394 | 7.911 | 15.40 |

CPU указан в процентах одного логического процессора Windows или одного ядра Pi. Pi process CPU вычисляется по schedstat потоков. Свободные ядра 2/3 сохранены. Max wake/RX gap/TX age описывает хвосты наблюдаемого окна, а не каждую audio callback.

| Прогон | eth0 RX/TX errors/drops | Pi RX/TX counter Mbps | Throttle |
| --- | --- | --- | --- |
| 15min-b64-g512-cb100 | 0/0/0/0 | 40.486/41.159 | throttled=0x50000 → throttled=0x50000 |
| graceful-reopen-0 | 0/0/0/0 | 38.820/39.502 | throttled=0x50000 → throttled=0x50000 |
| graceful-reopen-1 | 0/0/0/0 | 38.817/39.477 | throttled=0x50000 → throttled=0x50000 |
| graceful-reopen-2 | 0/0/0/0 | 38.536/39.251 | throttled=0x50000 → throttled=0x50000 |
| crash-owner-recovery | 0/0/0/0 | 39.485/40.151 | throttled=0x50000 → throttled=0x50000 |
| admin-link-recovery | 0/0/0/0 | 39.491/40.152 | throttled=0x50000 → throttled=0x50000 |

Счётчики eth0 bytes не имеют того же overhead, что расчёт полного wire budget. Одинаковое историческое throttled слово не исключает повторного события, чей history bit уже был выставлен. Эти снимки не заменяют измерение питания во время USB streaming.

## Владение устройством и восстановление

| Действующий владелец | Exit второго процесса | BUSY получен |
| --- | --- | --- |
| 15min-b64-g512-cb100 | 5 | True |

| Проверка | Наблюдение | Механизм | Recovery PCM |
| --- | --- | --- | --- |
| [crash-owner](evidence/lan-qualification-20261003-15min/crash-owner.json) | Убит созданный fixture процесс; immediate owner exit=5, BUSY=True; новое открытие после ожидания истечения 3-секундной lease | PASS | PASS |
| [admin-link-cycle](evidence/lan-qualification-20261003-15min/admin-link-cycle.json) | eth0 административно down на 10 с; Windows console owner exit=6; eth0 возвращён по timer и проверен через Wi-Fi SSH | PASS | PASS |

Повторное открытие проверено 3 отдельными обычными открытиями. Crash test завершает только процесс, созданный fixture. Link test использует административное выключение eth0; физическое извлечение кабеля этим не проверялось. После потери соединения запускается новый console owner: автоматический reconnect установленной SCM-службы здесь не проверен. Fault counters сохраняются отдельно и не выдаются за clean steady-state окно.

## Binary и возврат стенда

Windows service SHA256: `7e5a259e66dca0a66ac9c1ab7352c3ab8cec2d0dc0dca546e0e3f2f15aa11569`.

Испытанный Pi peer SHA256: `d54ef6ad5e04829119a51c143669b582965b966b0a281016a9113863067565aa`.

Перед тестом сняты hash executable, command и hashes конфигурации штатной службы. Кандидат запускается временным unit без замены установленного peer. 30-минутный watchdog возвращает службу при потере координатора; link timer поднимает eth0 через 10 с. После завершения исходный executable/command/config сравниваются со снимком.

[До теста](evidence/lan-qualification-20261003-15min/original.json) · [После восстановления](evidence/lan-qualification-20261003-15min/restored.json).

Первый сценарий, предусматривавший часовые проверки, остановлен по уточнению пользователя через несколько минут. Его незавершённый guard448 прогон не квалифицирован и не входит в таблицы PASS/FAIL или длительность этой серии. Данные и подтверждение возврата стенда сохранены отдельно в соседнем каталоге lan-qualification-20261003.

## Что остаётся для полного аудиотракта

Загрузка unsigned ACX драйвера на Windows-стенде, реальные WASAPI/KS periods и duplex ещё не проверены. ADC/DAC capture backend на Pi не подключён. Для реальной задержки нужны системный драйвер, подключённый audio backend и физический loopback. Все последующие проверочные прогоны ограничены 15 минутами по запросу пользователя. Несколько Pi остаются отложены. Возможности RIO/WSK/NDIS не являются условием завершения нынешней цифровой LAN-проверки.
