[English](TRANSPORT-2.5.md) | **Русский**

# Транспорт одного Pi, служба Windows и экспериментальный драйвер

PiAoIP 2.5.0 уменьшает работу памяти, ограничивает разбор приёма в callback и добавляет самостоятельную службу Windows. ASIO-адаптер сохраняется. Экспериментальный ACX/KMDF драйвер создаёт основу системных capture/render endpoints. Аппаратные проверки этой серии выполнены на одном Raspberry Pi 5 через встроенные Gigabit Ethernet порты.

## Реализация

| Компонент | Изменение и правило работы |
|---|---|
| Очереди Packet | Reserve/commit и acquire/release исключают копирование целого Packet. Один producer и consumer владеют своими слотами; reset допустим после остановки обоих. Переполнение учитывается. |
| Разбор RX | Между packets проверяется бюджет прохода min(period/4, 50 мкс). Просроченные frames отбрасываются с сохранением индексов. Это не гарантия планирования ОС. |
| Приём Windows | IOCP одной сессии использует overlapped buffers, ограниченные completions и отмену. Общий dispatcher нескольких Pi отложен. |
| PCM24 | На x86 — SSSE3 dispatch со scalar fallback; Pi ARM64 использует измеренный NEON row/scalar bulk путь. PCM16/24/32 сохраняет signed integer samples и корректно обрабатывает tails. |
| Linux datagrams | recvmmsg/sendmmsg обслуживает уже готовые packets без ожидания наполнения batch. Подтверждённый профиль использует socket batch=1. |
| Учёт трафика | Отдельно учитываются PCM, audio/UDP/IP/Ethernet overhead и packets/s каждого направления. Для VLAN/tunnels нужна другая модель. |
| DeviceId/session | Предпочтителен board serial; machine-id fallback требует уникальности образов. V3 lease отклоняет второго owner с BUSY. DeviceId не является аутентификацией. |
| Запуск workers | StartGate готовит MMCSS/timers/workers до подписки PCM, публикует подтверждённый epoch и освобождает потоки. Stop будит также ещё не запущенную сессию. |
| PCM bridge | Версионный ограниченный service/driver контракт проверяет channels, buffers, frame positions и владельца stream. METHOD_BUFFERED добавляет копирования/syscalls. |

Core остаётся C++17. Служба Windows собирается с `PIAOIP_BUILD_ASIO=OFF`; ASIO SDK нужен только прежнему адаптеру и его диагностическим hosts. Сохранён IEEE CRC: SSE4.2 CRC32C не является совместимой заменой.

## Подтверждённый профиль разработки

Поиск включал 15 коротких окон по 30 секунд и три отдельных теста по 900 секунд. Hashes binaries этого поиска указаны в измерениях; проверка пересобранного релиза документируется отдельно.

| Block / guard, callback 50 мкс | Время | Цифровой RTT p50 / p95 / p99 / max, мс | Ошибки | Итог |
|---|---:|---|---|---|
| 128 / 448 | 900 с | 2,908 / 3,145 / 3,221 / 8,075 | late/missing=5312; host_overruns=1 | FAIL |
| 256 / 1536 | 900 с | 8,918 / 9,419 / 9,553 / 13,532 | Error counters нулевые | PASS |
| 256 / 1024 | 900 с | 6,251 / 6,755 / 6,891 / 11,718 | Error counters нулевые | PASS |

Самый быстрый подтверждённый в этом поиске профиль: **8 входов + 8 выходов, 192 кГц, PCM24, capture32, block256, guard1024**, непрерывный PCM, EnergySaving=0. Guard=5,333 мс, callback period=1,333 мс. В финальном окне 3 373 249 RTT samples; callback/wake/RX gap/TX age max 118,2/1026,0/5839,4/642,8 мкс. Windows audio/RX/TX CPU — 7,212/3,017/1,929% одного логического процессора; Pi process CPU — 7,986% одного ядра.

Missing/late, deadline/skipped, resync, overruns, expired output, queue/socket/control/MMCSS/IO errors, Pi loss/error counters и приращения eth0 errors/drops — ноль. Reorder приведён отдельно. Current throttle flags в снимках до/после нулевые; историческое 0x50000 сохранялось. Питание непрерывно не измерялось.

| Направление | Расчётный wire Мбит/с | Packets/s |
|---|---:|---:|
| Capture Pi → Windows | 42,336 | 6000 |
| Render Windows → Pi | 40,284 | 3750 |

Снижение только guard1536→1024 уменьшило медиану RTT на 2,667 мс без изменения wire budget. Guard512/768 у block256 прошли только короткие окна; их 15-минутная устойчивость не подтверждена. RX gap — интервал наблюдения, не возраст каждого packet до presentation deadline. Итог определяется frame indices, фазой callback и реальными counters.

Измерен цифровой Pi → Windows service echo → Pi. Деление RTT на два не подтверждает одностороннюю capture/render latency. ADC/DAC, установленный ACX/WASAPI и реальная DAW здесь не измерялись. Buffers выбираются вручную; новые LAN-прогоны ограничены пятью минутами (300 секунд); прежние 900-секундные записи сохраняют фактическую длительность. Windows NIC/network priority не перестраивались.

## Системные endpoints и переход от ASIO

Самостоятельная служба владеет UDP, clock/routing, DeviceId и сессией. Kernel driver обслуживает audio streams, WaveRT packets, presentation positions и ограниченный обмен PCM. В audio kernel callback нет сетевых операций.

Сейчас поддержан один Pi с **одним multichannel capture endpoint и одним multichannel render endpoint**, до восьми каналов каждый. Dynamic child связан с DeviceId; reconnect/смена профиля завершают старые child/session. Независимые mono/stereo endpoints по каналам и aggregate нескольких Pi пока не квалифицированы.

Драйвер x64 — unsigned development build. Compiler/INF/catalog проверки не подтверждают установку, PnP, WASAPI или kernel runtime. ASIO MSI не устанавливает ACX. Остались signing, поддерживаемый MSVC/WDK build, enumeration, shared/raw/exclusive streams, positions, duplex, Driver Verifier, crash/removal/sleep, DAW и hardware loopback.

## USB и отложенные работы

Для USB предусмотрены отдельный репозиторий и библиотека транспорта. По решению владельца разработка сейчас отложена; USB-аудиоинтерфейса в релизе нет. Для одного DeviceId LAN будет default, переключение — явным, владение session — общим. High-speed USB microframe длится 125 мкс; у рассмотренного WinUSB isochronous группирования completions есть ограничение 1 мс. Bulk, vendor interrupt и isochronous требуют отдельных измеряемых прототипов: интервал шины 125 мкс не доказывает такую application audio latency. USB gadget/PCM этим выпуском не включаются.

Несколько Pi, aggregate clocks/ASRC, RIO A/B, ускоренный совместимый IEEE CRC, WSK и NDIS/L2 отложены. Исходники, dependency pins, пакеты и релиз Pi4 остаются прежними: в этой серии Pi4 не проверялся.

## Проверка и применение

Используйте совместимые компоненты 2.5.0. Существующие установленные профили Pi/Windows сохраняются; новый профиль выбирается явно. Rate/Bits принадлежат Pi: одноимённые Windows service INI поля его не перенастраивают. Новая установка Pi5 начинает с 8×8 / 192 кГц / PCM24 / capture32.

См. [сборку](BUILD.ru.md), [проверки](VALIDATION.ru.md) и [описание выпуска](RELEASE-2.5.0.ru.md). После поиска независимо подтверждён возврат исходного executable/config, LAN/Wi-Fi и CPU isolation Pi. Прежние FAIL, включая guard512 PCM recovery после link-cycle, сохраняются в evidence.


[Измерения, неудачные прогоны и хеши бинарников](https://github.com/danrey-bilo/Win11-asio-AoIP/blob/v2.5.0/docs/measurements/README.ru.md)


## Проверка пересобранного релиза

Свежие файлы: 300 секунд, block256/guard1536/callback50 — **PASS**. Цифровой RTT p50/p95/p99/max — **8.920/9.446/9.573/16.599 мс**, 1,123,192 samples. Основные Windows errors: ноль.

SHA-256 службы: `9b80dc78adc192cba79acbdfd6bb4a6ef7ba341cfe5862197782829bd1b38f63`; Pi5: `252a146bad247a75332b6710a52b04bd0a7360c3bae1fd47163d3e380a2aad1c`.

Callback/wake/RX gap/TX age max: 161.6/692.9/7270.0/884.0 мкс. Pi process CPU: 7.935% одного ядра. Полные CPU/counters/interface/throttle snapshots сохранены в JSON. Снимки питания до/после не являются непрерывным контролем.

Возврат исходных binary/config, LAN/Wi-Fi и CPU isolation независимо проверен. Hash peer в runtime DEB совпадает с проверенной нативной сборкой.

Предыдущий свежий block256/guard1024 выполнялся 900 с и **НЕ ПРОШЁЛ**: late/missing=1568/1568, RX gap max=7427.9 мкс. Низкая медиана и прежнее чистое окно не подтверждают надёжный минимум. Больший guard проверяется отдельным ручным профилем; wire budget тот же. Все последующие прогоны по решению владельца ограничены 300 секундами.

Ручная отправная настройка релиза: block256/guard1536, подтверждена за 300 секунд. Это не универсальная гарантия стабильности.
