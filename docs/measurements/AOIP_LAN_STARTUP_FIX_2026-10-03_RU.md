# Исправление запуска LAN одного Pi

Основной 300-секундный цифровой прогон: **FAIL**. Возврат исходной службы Pi и сети: **НЕ подтверждено**. Часовые тесты отменены по запросу пользователя.

Проверяется synthetic Pi → Windows service echo → Pi через встроенные Ethernet-порты. Это цифровой RTT, не задержка ADC/DAC и не подтверждение ACX/WASAPI или реальной DAW. Учитывается заданное пользователем ограничение Windows/LAN 1,2 мс; приоритеты Windows и настройки NIC не менялись.

Основной профиль: 8×8 каналов, 192000 Гц, PCM24, capture=32, block=64, guard=512, EnergySaving=0. Основной прогон имеет искусственную нагрузку callback 50 мкс. Короткие проверки reopen/recovery используют callback 50 мкс.

Окно сценария UTC: 2026-10-02T22:34:06.372693+00:00 — 2026-10-02T22:40:59.670458+00:00. Заданный суммарный PCM streaming в завершённых обычных прогонах: 375 секунд. Это отдельные открытия сессии, а не непрерывное суммарное аудиоокно. [Полный suite.json](evidence/lan-startup-fix-20261003/suite.json).

## Цифровой RTT

Время — мс; seconds задаёт длительность fixture, elapsed включает startup и сбор counters. RTT probe включается примерно спустя 0,5 с. Число RTT samples относится к маркерам, а не ко всем PCM frames. PASS требует нулевых error counters Windows, Pi и интерфейса; наличие перестановок пакетов само по себе не считается потерей.

| Прогон | Seconds | Elapsed | Block/guard | Callback us | RTT samples | p50 / p95 / p99 / max ms | Итог |
| --- | --- | --- | --- | --- | --- | --- | --- |
| [startup-reopen-0](evidence/lan-startup-fix-20261003/startup-reopen-0.json) | 3 | 3.27 | 64/512 | 50 | 15075 | 3.057 / 3.199 / 3.214 / 3.351 | PASS |
| [startup-reopen-1](evidence/lan-startup-fix-20261003/startup-reopen-1.json) | 3 | 3.27 | 64/512 | 50 | 15081 | 3.059 / 3.199 / 3.218 / 3.253 | PASS |
| [startup-reopen-2](evidence/lan-startup-fix-20261003/startup-reopen-2.json) | 3 | 3.28 | 64/512 | 50 | 15109 | 3.056 / 3.198 / 3.209 / 3.445 | PASS |
| [startup-reopen-3](evidence/lan-startup-fix-20261003/startup-reopen-3.json) | 3 | 3.27 | 64/512 | 50 | 15081 | 3.058 / 3.199 / 3.222 / 3.534 | PASS |
| [startup-reopen-4](evidence/lan-startup-fix-20261003/startup-reopen-4.json) | 3 | 3.27 | 64/512 | 50 | 15059 | 3.056 / 3.199 / 3.213 / 3.399 | PASS |
| [startup-reopen-5](evidence/lan-startup-fix-20261003/startup-reopen-5.json) | 3 | 3.26 | 64/512 | 50 | 15041 | 3.057 / 3.199 / 3.214 / 3.363 | PASS |
| [startup-reopen-6](evidence/lan-startup-fix-20261003/startup-reopen-6.json) | 3 | 3.26 | 64/512 | 50 | 15019 | 3.056 / 3.198 / 3.209 / 3.344 | PASS |
| [startup-reopen-7](evidence/lan-startup-fix-20261003/startup-reopen-7.json) | 3 | 3.27 | 64/512 | 50 | 15081 | 3.057 / 3.198 / 3.211 / 3.284 | PASS |
| [startup-reopen-8](evidence/lan-startup-fix-20261003/startup-reopen-8.json) | 3 | 3.27 | 64/512 | 50 | 15061 | 3.056 / 3.198 / 3.209 / 3.309 | PASS |
| [startup-reopen-9](evidence/lan-startup-fix-20261003/startup-reopen-9.json) | 3 | 3.27 | 64/512 | 50 | 15081 | 3.057 / 3.199 / 3.214 / 3.261 | PASS |
| [startup-reopen-10](evidence/lan-startup-fix-20261003/startup-reopen-10.json) | 3 | 3.27 | 64/512 | 50 | 15086 | 3.057 / 3.199 / 3.214 / 3.252 | FAIL |
| [startup-reopen-11](evidence/lan-startup-fix-20261003/startup-reopen-11.json) | 3 | 3.27 | 64/512 | 50 | 15079 | 3.056 / 3.199 / 3.211 / 3.257 | PASS |
| [startup-reopen-12](evidence/lan-startup-fix-20261003/startup-reopen-12.json) | 3 | 3.27 | 64/512 | 50 | 15071 | 3.056 / 3.198 / 3.209 / 3.269 | PASS |
| [startup-reopen-13](evidence/lan-startup-fix-20261003/startup-reopen-13.json) | 3 | 3.27 | 64/512 | 50 | 15051 | 3.057 / 3.199 / 3.210 / 3.445 | PASS |
| [startup-reopen-14](evidence/lan-startup-fix-20261003/startup-reopen-14.json) | 3 | 3.28 | 64/512 | 50 | 15101 | 3.057 / 3.203 / 3.213 / 3.274 | PASS |
| [startup-reopen-15](evidence/lan-startup-fix-20261003/startup-reopen-15.json) | 3 | 3.26 | 64/512 | 50 | 15041 | 3.057 / 3.199 / 3.215 / 3.249 | PASS |
| [startup-reopen-16](evidence/lan-startup-fix-20261003/startup-reopen-16.json) | 3 | 3.27 | 64/512 | 50 | 15062 | 3.056 / 3.199 / 3.213 / 3.308 | PASS |
| [startup-reopen-17](evidence/lan-startup-fix-20261003/startup-reopen-17.json) | 3 | 3.27 | 64/512 | 50 | 15096 | 3.056 / 3.199 / 3.214 / 3.273 | PASS |
| [startup-reopen-18](evidence/lan-startup-fix-20261003/startup-reopen-18.json) | 3 | 3.26 | 64/512 | 50 | 15033 | 3.056 / 3.199 / 3.216 / 3.393 | PASS |
| [startup-reopen-19](evidence/lan-startup-fix-20261003/startup-reopen-19.json) | 3 | 3.27 | 64/512 | 50 | 15077 | 3.057 / 3.199 / 3.215 / 3.269 | PASS |
| [startup-regression-b64-g512-cb50](evidence/lan-startup-fix-20261003/startup-regression-b64-g512-cb50.json) | 300 | 300.27 | 64/512 | 50 | 1797074 | 3.057 / 3.151 / 3.214 / 10.045 | FAIL |
| [crash-owner-recovery](evidence/lan-startup-fix-20261003/crash-owner-recovery.json) | 15 | 15.27 | 64/512 | 50 | 87085 | 3.058 / 3.189 / 3.216 / 3.586 | PASS |

## Ошибки сроков и очередей Windows

| Прогон | late/missing frames | deadline/skipped frames | overrun/expired output frames | invalid/RX overflow | TX overflow/expired/errors |
| --- | --- | --- | --- | --- | --- |
| startup-reopen-0 | 0/0 | 0/0 | 0/0 | 0/0 | 0/0/0 |
| startup-reopen-1 | 0/0 | 0/0 | 0/0 | 0/0 | 0/0/0 |
| startup-reopen-2 | 0/0 | 0/0 | 0/0 | 0/0 | 0/0/0 |
| startup-reopen-3 | 0/0 | 0/0 | 0/0 | 0/0 | 0/0/0 |
| startup-reopen-4 | 0/0 | 0/0 | 0/0 | 0/0 | 0/0/0 |
| startup-reopen-5 | 0/0 | 0/0 | 0/0 | 0/0 | 0/0/0 |
| startup-reopen-6 | 0/0 | 0/0 | 0/0 | 0/0 | 0/0/0 |
| startup-reopen-7 | 0/0 | 0/0 | 0/0 | 0/0 | 0/0/0 |
| startup-reopen-8 | 0/0 | 0/0 | 0/0 | 0/0 | 0/0/0 |
| startup-reopen-9 | 0/0 | 0/0 | 0/0 | 0/0 | 0/0/0 |
| startup-reopen-10 | 0/0 | 0/0 | 0/0 | 0/0 | 0/0/0 |
| startup-reopen-11 | 0/0 | 0/0 | 0/0 | 0/0 | 0/0/0 |
| startup-reopen-12 | 0/0 | 0/0 | 0/0 | 0/0 | 0/0/0 |
| startup-reopen-13 | 0/0 | 0/0 | 0/0 | 0/0 | 0/0/0 |
| startup-reopen-14 | 0/0 | 0/0 | 0/0 | 0/0 | 0/0/0 |
| startup-reopen-15 | 0/0 | 0/0 | 0/0 | 0/0 | 0/0/0 |
| startup-reopen-16 | 0/0 | 0/0 | 0/0 | 0/0 | 0/0/0 |
| startup-reopen-17 | 0/0 | 0/0 | 0/0 | 0/0 | 0/0/0 |
| startup-reopen-18 | 0/0 | 0/0 | 0/0 | 0/0 | 0/0/0 |
| startup-reopen-19 | 0/0 | 0/0 | 0/0 | 0/0 | 0/0/0 |
| startup-regression-b64-g512-cb50 | 1440/1440 | 0/0 | 0/0 | 0/0 | 0/0/0 |
| crash-owner-recovery | 0/0 | 0/0 | 0/0 | 0/0 | 0/0/0 |

Связанные late/missing/skipped counters нельзя складывать как уникальные потери.

| Прогон | control/MMCSS/resync | IO errors/overflows | Pi lost/bad/skipped/duplicate/expired | Pi reorder/retries |
| --- | --- | --- | --- | --- |
| startup-reopen-0 | 0/0/0 | 0/0 | 0/0/0/0/0 | 0/0 |
| startup-reopen-1 | 0/0/0 | 0/0 | 0/0/0/0/0 | 0/0 |
| startup-reopen-2 | 0/0/0 | 0/0 | 0/0/0/0/0 | 1/0 |
| startup-reopen-3 | 0/0/0 | 0/0 | 0/0/0/0/0 | 2/0 |
| startup-reopen-4 | 0/0/0 | 0/0 | 0/0/0/0/0 | 5/0 |
| startup-reopen-5 | 0/0/0 | 0/0 | 0/0/0/0/0 | 0/0 |
| startup-reopen-6 | 0/0/0 | 0/0 | 0/0/0/0/0 | 3/0 |
| startup-reopen-7 | 0/0/0 | 0/0 | 0/0/0/0/0 | 3/0 |
| startup-reopen-8 | 0/0/0 | 0/0 | 0/0/0/0/0 | 5/0 |
| startup-reopen-9 | 0/0/0 | 0/0 | 0/0/0/0/0 | 2/0 |
| startup-reopen-10 | 0/0/0 | 0/0 | 0/0/0/0/0 | 0/0 |
| startup-reopen-11 | 0/0/0 | 0/0 | 0/0/0/0/0 | 2/0 |
| startup-reopen-12 | 0/0/0 | 0/0 | 0/0/0/0/0 | 1/0 |
| startup-reopen-13 | 0/0/0 | 0/0 | 0/0/0/0/0 | 4/0 |
| startup-reopen-14 | 0/0/0 | 0/0 | 0/0/0/0/0 | 4/0 |
| startup-reopen-15 | 0/0/0 | 0/0 | 0/0/0/0/0 | 7/0 |
| startup-reopen-16 | 0/0/0 | 0/0 | 0/0/0/0/0 | 1/0 |
| startup-reopen-17 | 0/0/0 | 0/0 | 0/0/0/0/0 | 1/0 |
| startup-reopen-18 | 0/0/0 | 0/0 | 0/0/0/0/0 | 0/0 |
| startup-reopen-19 | 0/0/0 | 0/0 | 0/0/0/0/0 | 3/0 |
| startup-regression-b64-g512-cb50 | 0/0/0 | 0/0 | 0/0/0/0/0 | 337/0 |
| crash-owner-recovery | 0/0/0 | 0/0 | 0/0/0/0/0 | 8/0 |

## CPU и максимальные интервалы

| Прогон | callback/wake/RX gap/TX age max us | Win audio/RX/TX % | Pi process % | Pi CPU window s |
| --- | --- | --- | --- | --- |
| startup-reopen-0 | 55.4/224.5/495.7/132.7 | 21.827/3.638/4.677 | 7.022 | 3.48 |
| startup-reopen-1 | 74.0/183.2/516.6/132.5 | 20.259/3.636/4.675 | 7.170 | 3.43 |
| startup-reopen-2 | 97.6/177.6/492.4/282.9 | 21.783/2.593/2.593 | 7.054 | 3.43 |
| startup-reopen-3 | 89.0/164.4/298.0/330.7 | 20.779/2.597/5.195 | 7.039 | 3.43 |
| startup-reopen-4 | 99.7/169.1/333.0/348.0 | 21.843/3.120/4.161 | 6.952 | 3.43 |
| startup-reopen-5 | 97.2/221.2/408.3/113.3 | 21.867/3.644/2.083 | 6.979 | 3.44 |
| startup-reopen-6 | 135.1/168.6/467.1/272.1 | 20.850/2.085/2.606 | 6.919 | 3.43 |
| startup-reopen-7 | 91.9/229.0/336.7/233.1 | 21.300/4.676/4.156 | 7.026 | 3.43 |
| startup-reopen-8 | 130.5/197.5/493.5/250.2 | 18.720/1.040/3.120 | 6.972 | 3.43 |
| startup-reopen-9 | 151.4/172.9/267.3/59.6 | 16.095/6.230/3.115 | 6.963 | 3.42 |
| startup-reopen-10 | 77.2/160.2/384.4/50.5 | 22.847/3.635/3.635 | 6.961 | 3.44 |
| startup-reopen-11 | 100.3/179.6/256.7/68.6 | 18.179/4.155/5.194 | 7.005 | 3.43 |
| startup-reopen-12 | 143.9/180.7/348.2/106.7 | 20.268/2.599/1.559 | 6.997 | 3.44 |
| startup-reopen-13 | 143.4/159.5/372.3/234.1 | 19.770/4.162/2.081 | 7.041 | 3.42 |
| startup-reopen-14 | 117.2/204.1/321.0/76.5 | 21.273/4.670/1.557 | 6.930 | 3.48 |
| startup-reopen-15 | 99.0/210.3/255.5/69.1 | 21.864/2.082/3.644 | 6.963 | 3.43 |
| startup-reopen-16 | 122.3/199.6/260.6/90.6 | 21.319/1.040/3.120 | 6.918 | 3.44 |
| startup-reopen-17 | 107.5/192.1/262.8/82.6 | 19.204/1.557/3.114 | 7.006 | 3.42 |
| startup-reopen-18 | 105.3/207.9/298.0/195.9 | 19.270/0.521/4.166 | 7.012 | 3.43 |
| startup-reopen-19 | 86.6/181.4/235.4/102.5 | 18.704/2.598/4.157 | 7.037 | 3.43 |
| startup-regression-b64-g512-cb50 | 220.3/1106.5/7522.1/443.9 | 23.515/3.057/3.922 | 8.233 | 300.49 |
| crash-owner-recovery | 130.1/321.6/464.2/116.5 | 24.050/3.228/2.499 | 8.075 | 15.39 |

CPU указан в процентах одного логического процессора Windows или одного ядра Pi. Pi process CPU вычисляется по schedstat потоков. Свободные ядра 2/3 сохранены. Max wake/RX gap/TX age описывает хвосты наблюдаемого окна, а не каждую audio callback.

| Прогон | eth0 RX/TX errors/drops | Pi RX/TX counter Mbps | Throttle |
| --- | --- | --- | --- |
| startup-reopen-0 | 0/0/0/0 | 34.957/35.582 | throttled=0x50000 → throttled=0x50000 |
| startup-reopen-1 | 0/0/0/0 | 35.485/36.119 | throttled=0x50000 → throttled=0x50000 |
| startup-reopen-2 | 0/0/0/0 | 35.565/36.202 | throttled=0x50000 → throttled=0x50000 |
| startup-reopen-3 | 0/0/0/0 | 35.468/36.101 | throttled=0x50000 → throttled=0x50000 |
| startup-reopen-4 | 0/0/0/0 | 35.420/36.052 | throttled=0x50000 → throttled=0x50000 |
| startup-reopen-5 | 0/0/0/0 | 35.321/35.952 | throttled=0x50000 → throttled=0x50000 |
| startup-reopen-6 | 0/0/0/0 | 35.339/35.970 | throttled=0x50000 → throttled=0x50000 |
| startup-reopen-7 | 0/0/0/0 | 35.533/36.167 | throttled=0x50000 → throttled=0x50000 |
| startup-reopen-8 | 0/0/0/0 | 35.490/36.123 | throttled=0x50000 → throttled=0x50000 |
| startup-reopen-9 | 0/0/0/0 | 35.595/36.230 | throttled=0x50000 → throttled=0x50000 |
| startup-reopen-10 | 0/0/0/0 | 35.427/36.060 | throttled=0x50005 → throttled=0x50000 |
| startup-reopen-11 | 0/0/0/0 | 35.501/36.137 | throttled=0x50000 → throttled=0x50000 |
| startup-reopen-12 | 0/0/0/0 | 35.405/36.036 | throttled=0x50000 → throttled=0x50000 |
| startup-reopen-13 | 0/0/0/0 | 35.534/36.168 | throttled=0x50000 → throttled=0x50000 |
| startup-reopen-14 | 0/0/0/0 | 35.015/35.640 | throttled=0x50000 → throttled=0x50000 |
| startup-reopen-15 | 0/0/0/0 | 35.392/36.024 | throttled=0x50000 → throttled=0x50000 |
| startup-reopen-16 | 0/0/0/0 | 35.401/36.033 | throttled=0x50000 → throttled=0x50000 |
| startup-reopen-17 | 0/0/0/0 | 35.600/36.235 | throttled=0x50000 → throttled=0x50000 |
| startup-reopen-18 | 0/0/0/0 | 35.392/36.025 | throttled=0x50000 → throttled=0x50000 |
| startup-reopen-19 | 0/0/0/0 | 35.494/36.130 | throttled=0x50000 → throttled=0x50000 |
| startup-regression-b64-g512-cb50 | 0/0/0/0 | 40.448/41.120 | throttled=0x50000 → throttled=0x50000 |
| crash-owner-recovery | 0/0/0/0 | 39.495/40.163 | throttled=0x50000 → throttled=0x50000 |

Счётчики eth0 bytes не имеют того же overhead, что расчёт полного wire budget. Одинаковое историческое throttled слово не исключает повторного события, чей history bit уже был выставлен. Эти снимки не заменяют измерение питания во время USB streaming.

## Владение устройством и восстановление

| Действующий владелец | Exit второго процесса | BUSY получен |
| --- | --- | --- |
| startup-reopen-0 | 5 | True |
| startup-regression-b64-g512-cb50 | 5 | True |

| Проверка | Наблюдение | Механизм | Recovery PCM |
| --- | --- | --- | --- |
| [crash-owner](evidence/lan-startup-fix-20261003/crash-owner.json) | Убит созданный fixture процесс; immediate owner exit=5, BUSY=True; новое открытие после ожидания истечения 3-секундной lease | PASS | PASS |

Повторное открытие проверено 20 отдельными обычными открытиями. Crash test завершает только процесс, созданный fixture. Link test использует административное выключение eth0; физическое извлечение кабеля этим не проверялось. После потери соединения запускается новый console owner: автоматический reconnect установленной SCM-службы здесь не проверен. Fault counters сохраняются отдельно и не выдаются за clean steady-state окно.

## Binary и возврат стенда

Windows service SHA256: `9fe74f024a9bffb4fe427af261b3264dad60cc191c96af0559cc15d8708233bc`.

Испытанный Pi peer SHA256: `d54ef6ad5e04829119a51c143669b582965b966b0a281016a9113863067565aa`.

Перед тестом сняты hash executable, command и hashes конфигурации штатной службы. Кандидат запускается временным unit без замены установленного peer. 30-минутный watchdog возвращает службу при потере координатора; link timer поднимает eth0 через 10 с. После завершения исходный executable/command/config сравниваются со снимком.

[До теста](evidence/lan-startup-fix-20261003/original.json) · [После восстановления](evidence/lan-startup-fix-20261003/restored.json).

Первый сценарий, предусматривавший часовые проверки, остановлен по уточнению пользователя через несколько минут. Его незавершённый guard448 прогон не квалифицирован и не входит в таблицы PASS/FAIL или длительность этой серии. Данные и подтверждение возврата стенда сохранены отдельно в соседнем каталоге lan-qualification-20261003.

## Что остаётся для полного аудиотракта

Загрузка unsigned ACX драйвера на Windows-стенде, реальные WASAPI/KS periods и duplex ещё не проверены. ADC/DAC capture backend на Pi не подключён. Для реальной задержки нужны системный драйвер, подключённый audio backend и физический loopback. Все последующие проверочные прогоны ограничены 15 минутами по запросу пользователя. Несколько Pi остаются отложены. Возможности RIO/WSK/NDIS не являются условием завершения нынешней цифровой LAN-проверки.

Координатор отметил возврат исходной службы: executable, command и config совпали. Однако сохранённый restored.json ещё показывал eth0 DOWN без IPv4. Поэтому **полный возврат сети в этом снимке не подтверждён**. Проверка отчёта требует eth0 UP с 192.168.1.2/24; позднейшая доступность LAN не подменяет исторический снимок. В следующей серии margin LAN снова доступен, что видно в её исходном снимке.

Ошибка координатора: `RuntimeError: Fixed Pi test operation failed: Failed to stop piaoip-lan-qualification-20261003-link-restore.timer: Unit piaoip-lan-qualification-20261003-link-restore.timer not loaded.`. Непроведённые проверки не считаются пройденными.

## Границы проверки startup fix

В новой службе добавлен StartGate: настройка audio/RX/TX workers и таймеров завершается до подписки на удалённый PCM. Epoch подтверждённой подписки публикуется до начала рабочих циклов. При отказе подписки или stop незапущенные workers пробуждаются и завершаются. Gate находится в отдельном platform header и не добавляет ожидание или выделение памяти на каждом PCM callback.

Windows CTest: 13/13 PASS, включая отложенную готовность worker, публикацию epoch, отмену до выпуска gate и настоящий UDP ownership/callback failure/reopen. [CTest log](evidence/lan-startup-fix-20261003/ctest.log).

Все 20 коротких reopen имеют нулевые PCM/transport error counters. Аппаратная квалификация: 19/20 PASS; в startup-reopen-10 снимок до открытия показал `throttled=0x50005`, после — `0x50000`. Биты 0 и 2 означают текущие undervoltage и throttling. Это событие питания, а не ошибка PCM в данном трёхсекундном окне. Причина отдельного большого RX gap этим не установлена. [Описание флагов Pi](https://www.raspberrypi.com/documentation/computers/os.html#get_throttled).

Link-cycle recovery в этой серии не завершён: после срабатывания timer `--collect` уже удалил unit, и координатор ошибочно счёл его отсутствие ошибкой stop. Обработка отсутствующего fixed timer исправлена в test harness; новая проверка выполняется отдельной серией margin, её исход не приписывается этой серии.

Новая сборка проверена основным прогоном 300 секунд и короткими reopen/fault проверками. Исходный 15-минутный тест относится к прежнему binary и другой нагрузке callback; его FAIL не отменяется. [Исходный 15-минутный отчёт](AOIP_LAN_15MIN_VALIDATION_2026-10-03_RU.md).

StartGate устраняет окно начала передачи до настройки workers. Он не устраняет произвольные задержки планировщика Windows, не исправляет steady-state TX starvation и не гарантирует отсутствия callback overrun при любой нагрузке.
