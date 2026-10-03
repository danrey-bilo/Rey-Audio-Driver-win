Current USB/LAN service and panel: [Rey Audio Driver 2.6.0](REY-AUDIO-2.6.ru.md).

[English](ACX-SERVICE.md) | **Русский**

# Служба для одного Pi и экспериментальный драйвер ACX

В этой ветке есть Windows-служба без зависимости от ASIO SDK и первая реализация собственного ACX/KMDF драйвера. Поддерживается один Pi: один многоканальный capture endpoint и один render endpoint, до восьми каналов в каждом направлении. Выбор меньшего числа каналов меняет формат endpoint. Отдельные mono/stereo устройства и несколько физических Pi пока отложены.

Служба проверена настоящими UDP-сессиями и конечными цифровыми тестами через встроенные Ethernet-порты. Драйвер собран для x64, INF прошёл InfVerif и Inf2Cat. **Он ещё не подписан, не установлен, не загружен и не проверен через WASAPI.** Сборка не подтверждает безопасность ядра или работоспособность системных аудиоустройств. Текущий backend Pi генерирует синтетический PCM; подключённого ADC/DAC тракта нет.

## Структура и владение

| Компонент | Задача |
|---|---|
| `AoIP-lib` | Протокол, CRC, Timeline с индексами кадров, PCM, маски каналов, бюджет трафика |
| `src/engine/PeerSession` | DeviceId, обнаружение, подписка V3, lease, keepalive, освобождение сессии |
| `src/engine/SessionEngine` | Приём IOCP, audio callback по срокам, ограничение RX и очередь TX |
| `src/platform/StartGate` | Однократное ожидание готовности workers и публикация epoch до начала удалённого PCM |
| `src/transport/IocpReceiver` | Пул overlapped-буферов, владение слотами, отмена и завершение I/O |
| `src/bridge/DriverBridge` | Версионированный контракт обмена ограниченными блоками PCM |
| `apps/aoip_service.cpp` | Console/SCM, reconnect и цифровой echo для стенда |
| `drivers/Acx` | Управляющее root-устройство, динамический child, circuits и WaveRT |
| `apps/wasapi_probe.cpp` | Список endpoints, периоды IAudioClient3 и конечный shared-поток |

Прежний ASIO adapter сохранён. Он использует общие transport/PCM компоненты, но его аудиопланировщик ещё не полностью переведён на SessionEngine. Это ограничение переходной реализации; единый движок для обоих адаптеров пока не заявляется как завершённый.

SessionEngine настраивает audio/RX/TX workers, их MMCSS и таймеры до подписки на peer. StartGate выпускает их после публикации подтверждённого epoch. Stop пробуждает workers, чья сессия ещё не была выпущена. Gate не добавляет ожидание или выделение памяти в рабочий PCM callback. Новая служба прошла 13/13 Windows component tests, включая задержанную готовность, публикацию epoch, отмену запуска и настоящее UDP повторное открытие.

DeviceId Pi берётся прежде всего из серийного номера платы. Явный `PIAOIP_DEVICE_ID` должен содержать 32 ненулевых строчных hex-символа; неверное значение отклоняется. Fallback machine-id требует уникальности при клонировании образов. Вторая V3-сессия получает BUSY, пока действует lease первой. DeviceId и lease управляют владением; это не криптографическая аутентификация сети.

Управляющий kernel-интерфейс доступен SYSTEM/администраторам и допускает одного владельца. Служба создаёт child; закрытие её handle запрашивает удаление endpoints. Аудиоприложения работают с обычными audio endpoints. Текущий bridge использует фиксированный METHOD_BUFFERED запрос: копирования и системный вызов на каждый PCM-блок. Это первоначальный контракт, его overhead ещё нужно измерить.

## Сборка без ASIO

Нужны Windows x64, CMake 3.20+, Ninja и полный LLVM-MinGW либо MSVC с Windows SDK. В общей рабочей папке укажите актуальный AoIP-lib:

```powershell
cmake -S . -B build/service -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DPIAOIP_BUILD_ASIO=OFF -DPIAOIP_BUILD_SERVICE=ON `
  -DAOIP_SOURCE_DIR=C:/work/AoIP-lib
cmake --build build/service --parallel 2
```

Результат — `PiAoipService.exe` и `PiAoipWasapiProbe.exe`. Эта сборка не требует заголовков Steinberg, регистрации ASIO или ASIO DLL. Для урезанного MinGW пути к недостающим стандартным заголовкам и import library задаются через `PIAOIP_WASAPI_HEADERS` и `PIAOIP_SETUPAPI_LIBRARY`. Эти файлы toolchain не включаются в пакет проекта.

Поддерживаемый путь сборки kernel-драйвера — Visual Studio 2022, x64 MSVC и согласованные Windows SDK/WDK 26100. Проект задаёт KMDF 1.31 и ACX 1.1:

```powershell
msbuild drivers/Acx/ReyAudioAcx.vcxproj /p:Configuration=Release /p:Platform=x64
```

Этот MSVC-проект ещё не запускался на текущем ПК: интеграция MSVC/WDK здесь не установлена. `tools/build_acx.py` выполняет отдельную экспериментальную сборку Clang 22 с MSVC ABI, официальными headers/libraries, проверкой InfVerif/Inf2Cat и hash-манифестом. Скрипт не подписывает и не устанавливает драйвер. Перед квалификацией нужны сборка поддерживаемым toolchain и проверка ABI/ассемблера нестандартной сборки. SDK/WDK libraries и исходники примеров Microsoft не являются исходниками проекта для распространения.

## Цифровой тест транспорта

На выделенном LAN должен работать обновлённый peer Pi с `PIAOIP_IDENTITY_V1`. Для измеренного режима настройте **сам Pi** на 192000 Гц, восемь входов/выходов, PCM24 и capture packet 32 кадра. Windows-служба наследует rate/bits peer; строки INI `Rate`/`Bits` не меняют удалённый профиль.

```powershell
PiAoipService.exe --console --echo --seconds 300 --peer 192.168.1.2 `
  --block 256 --guard 1536 --frames 32 --callback-us 50 --no-energy
```

Echo проверяет цифровой транспорт. Он не создаёт Windows endpoints и не измеряет аналоговые преобразователи. `SERVICE_STATS`/`SERVICE_IO` показывают missing/late frames, deadline/skipped frames, ошибки очередей/сокетов, время callback, receive gaps и CPU потоков. Просроченный PCM отбрасывается с сохранением индексов кадров. Скрытого изменения ручных буферов нет.

`config/service.example.ini` теперь содержит ручной block256/guard1536, проверенный на свежих файлах за 300 секунд. Свежий guard1024 получил late/missing frames. Уже установленные INI-файлы не переписываются. Последующие block64/guard512 проверки выявили сбои: в 900-секундном прогоне один host overrun; в 300-секундном — 1440 late/missing frames и receive gap 7,522 мс. Этот guard не является подтверждённо устойчивой отправной точкой. Больший guard увеличивает задержку и выбирается явно; автоматической перестройки нет. Новые LAN-прогоны ограничены **300 секундами**. Для SCM нужен абсолютный путь INI. `[Service] DeviceId` фиксирует плату; пустое значение запоминает первый DeviceId на время процесса. Подключение другой платы завершает этот запуск службы.

Новая служба завершила ещё 900 секунд с block64/guard1536/callback50. Missing/late/deadline/queue errors нулевые, но host_overruns=1, поэтому полная qualification остаётся FAIL. Цифровой RTT p50/p95/p99/max: 8,392/8,532/8,567/13,446 мс. Больший guard не меняет callback period 333,3 мкс при block64/192 кГц. Crash/lease recovery прошёл; административный возврат LAN и новое владение сработали, но отдельный guard512 PCM recovery получил 3488 late/missing frames и 9 просроченных TX packets. После серии подтверждён возврат исходной службы Pi, конфигурации, LAN/Wi-Fi и остановка тестовых units. SCM reconnect и реальная DAW этими результатами не квалифицированы.

Следующий конечный поиск профиля включал 15 коротких окон и три отдельных 900-секундных теста на неизменных binaries. Block128/guard448 не прошёл: 5312 late/missing frames и один host overrun. Block256/guard1536 и block256/guard1024 прошли с нулевыми PCM, deadline, overrun, queue/socket/IO, Pi loss/error counters и приращениями eth0 errors/drops. Самый быстрый профиль, подтверждённый 15 минутами в этом поиске: **block256/guard1024/callback50**, 8 входов/выходов, 192 кГц, PCM24 и capture32. Цифровой RTT p50/p95/p99/max: **6,251/6,755/6,891/11,718 мс**; 3 373 249 samples. Windows audio/RX/TX CPU — 7,212/3,017/1,929% одного логического процессора; Pi process CPU — 7,986% одного ядра. Wire budget SERVICE_READY — capture 42,336 Мбит/с, render 40,284 Мбит/с. Меньшие guard512/768 у block256 прошли только 30-секундные окна и не подтверждены как стабильные. Исходный стенд восстановлен и отдельно проверен. Подтверждено указанное непрерывное цифровое окно; recovery при guard1024, SCM, ACX/WASAPI, реальная DAW и физический ADC/DAC здесь не проверены. Команда выше использует больший профиль из проверки пересобранного релиза; сохранённый INI автоматически не меняется.

## Стенд kernel-драйвера и WASAPI

Нужен отдельный Windows x64 стенд/VM с возможностью восстановления, kernel debugging, сборкой поддерживаемым toolchain и подготовленной тестовой подписью. Приложенные SYS/CAT не подписаны. Изменение подписи, boot security и перезагрузка рабочего Windows требуют отдельного явного решения. Кроме INF нужно создать root-устройство: одно добавление пакета в Driver Store его не создаёт. На уже подготовленном стенде WDK DevCon использует `devcon install ReyAudioAcx.inf Root\ReyAudioAcx`.

После установки подписанного root-драйвера и запуска нового peer Pi конечный ACX-тест из повышенной консоли:

```powershell
PiAoipService.exe --console --seconds 30 --config C:/test/service.ini
```

Во второй консоли, пока служба работает:

```powershell
PiAoipWasapiProbe.exe
PiAoipWasapiProbe.exe --id "endpoint-id-из-списка" --flow capture --seconds 10
PiAoipWasapiProbe.exe --id "endpoint-id-из-списка" --flow render --raw --seconds 10 --period 128
```

По умолчанию выбираются endpoints PiAoIP. `--all` только читает список активных устройств. Render пишет цифровую тишину, capture отбрасывает данные. Инструмент читает default/fundamental/min/max periods и проверяет допустимость shared period перед открытием. Percentiles event gaps описывают пробуждения приложения, а не ADC/DAC latency. Пока реализована проверка shared mode; exclusive, одновременный duplex, сохранность PCM и длительная реальная DAW требуют отдельной квалификации. Windows может предоставить иной период, чем заявлено в constraints драйвера. [Периоды IAudioClient3](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclient3-getsharedmodeengineperiod), [открытие shared-потока](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclient3-initializesharedaudiostream).

До выпуска проверить Driver Verifier, параллельное открытие/закрытие streams, crash службы, истечение lease, отключение/возврат кабеля, удаление child, неверные профили, wrap номера пакета, sleep/resume и удаление root. Проверить позиции/QPC, capture overruns/render underruns, shared/raw/exclusive negotiation и отмену kernel I/O. Ограниченный PCM-цикл не гарантирует ограниченное время синхронного DeviceIoControl. Измерить цену service/driver/Windows audio engine отдельно от сетевого RTT. Для физической задержки нужен ADC/DAC backend и аппаратный loopback.


## Проверка пересобранного релиза

Свежие файлы: 300 секунд, block256/guard1536/callback50 — **PASS**. Цифровой RTT p50/p95/p99/max — **8.920/9.446/9.573/16.599 мс**, 1,123,192 samples. Основные Windows errors: ноль.

SHA-256 службы: `9b80dc78adc192cba79acbdfd6bb4a6ef7ba341cfe5862197782829bd1b38f63`; Pi5: `252a146bad247a75332b6710a52b04bd0a7360c3bae1fd47163d3e380a2aad1c`.

Callback/wake/RX gap/TX age max: 161.6/692.9/7270.0/884.0 мкс. Pi process CPU: 7.935% одного ядра. Полные CPU/counters/interface/throttle snapshots сохранены в JSON. Снимки питания до/после не являются непрерывным контролем.

Возврат исходных binary/config, LAN/Wi-Fi и CPU isolation независимо проверен. Hash peer в runtime DEB совпадает с проверенной нативной сборкой.

Предыдущий свежий block256/guard1024 выполнялся 900 с и **НЕ ПРОШЁЛ**: late/missing=1568/1568, RX gap max=7427.9 мкс. Низкая медиана и прежнее чистое окно не подтверждают надёжный минимум. Больший guard проверяется отдельным ручным профилем; wire budget тот же. Все последующие прогоны по решению владельца ограничены 300 секундами.
