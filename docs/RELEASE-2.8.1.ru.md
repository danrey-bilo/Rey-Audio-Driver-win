[English](RELEASE-2.8.1.md) · **Русский**

# Rey Audio Driver 2.8.1 USB preview

Тег **v2.8.1-usb-preview.1** · одна Pi5 · Windows x64 · USB-ASIO 8×8.

Обновлена постановка USB IN и документированы ручные профили 96/192 кГц
с доказательствами установленной сборки. Pi5-AUSB 0.1.1 добавляет FIFO40 и
блокировку памяти на стенде. Панель яснее показывает настройки ASIO, рабочий
блок и расчёт задержки. Установщик пропускает идентичные файлы, сохраняя
проверку блокировок и откат для изменившихся файлов.

Контракты Windows **32/32**, транспорт **7/7 на Windows и Pi5**, SDK consumer
0.1.1 EXACT **1/1**, финальная матрица PCM **6/6**. Native-профили
192/64/запас3/depth4 и 96/32/запас3/depth4 прошли **по175 с без ошибок PCM/deadline**.
RTT p99: **1,5280 / 1,5199 мс**, max: **4,3675 / 3,1292 мс**. Реальные профили
Ableton прошли **по 175 с с CPU Usage Simulator 50%** без drops/late/missing/overflow.
ASIO сообщает **1,5052 / 1,5104 мс**. Строгий максимум ниже 3 мс пока не достигнут.
Реальные наблюдения Ableton и отклонённые профили
сохранены в [отчёте](USB-LATENCY-2.8.1.ru.md). Физический ADC/DAC не измерен.

| Файл | Состав |
|---|---|
| `Rey-Audio-USB-ASIO-2.8.1-preview.1-x64.exe` | UAC-установщик: ASIO, автоматическая служба, микшер/трей |
| `Rey-Audio-USB-ASIO-2.8.1-preview.1-x64.zip` | Тот же EXE, EN/RU документация, скриншоты и доказательства |
| `Rey-Audio-USB-ASIO-2.8.1-preview.1-source.zip` | Исходники коммита без внешних SDK headers/binaries |
| `SHA256.json` | Hashes встроенных файлов и установщика |
| `SHA256SUMS-2.8.1-preview.1.txt` | Контрольные суммы скачиваемых файлов |

[Скачать](https://github.com/danrey-bilo/Rey-Audio-Driver-win/releases/tag/v2.8.1-usb-preview.1) · [Установка](USB-ASIO.ru.md) · [Данные](evidence/rey-usb-latency-281-20261003.json).
Новый драйвер ядра, сертификат, Test Mode, INI и смена загрузки/защиты не нужны.
Системные устройства Windows, физический hotplug/sleep/resume и удаление —
отдельные этапы. AoIP/Pi4 и прежние release assets не меняются.
