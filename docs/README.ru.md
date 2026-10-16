[English](README.md) · **Русский**

# Документация Rey Audio Driver

Rey Audio Driver 2.8.1 — предварительная USB-ASIO версия для одной Raspberry
Pi 5 и аудио 8×8. Ветка разработки 2.8.2 добавляет AKM USB/JACK и фиксированные
параметры транспорта для каждой частоты. Начните с установки, микшера и буфера ASIO.

| Задача | Документ |
|---|---|
| Скачать установщик Windows | [Релиз USB preview](https://github.com/danrey-bilo/Rey-Audio-Driver-win/releases/tag/v2.8.1-usb-preview.2) |
| Установить и выбрать драйвер в Ableton | [USB-ASIO](USB-ASIO.ru.md) |
| Выбрать буфер ASIO | [Буферы](BUFFER-GUIDE.ru.md) |
| Посмотреть текущую цифровую проверку AKM USB | [Измерения 16 октября](USB-LATENCY-2026-10-16.md) |
| Управлять каналами и индикаторами | [Микшер](MIXER.ru.md) |
| Выбрать аудиокарту или комбинированный режим AKM | [Управление режимами и условия USB](AKM-OPERATING-MODES.ru.md) |
| Посмотреть измерения и ограничения | [Проверка 96/192 кГц](USB-LATENCY-2.8.1.ru.md) · [Исходные данные](evidence/rey-usb-latency-281-20261003.json) |
| Узнать изменения релиза | [Описание релиза](RELEASE-2.8.1.ru.md) |
| Собрать ПО и установщик | [Сборка](BUILD.ru.md) · [Внешний ASIO SDK](ASIO-SDK.ru.md) |
| Разобраться в аудиотракте | [Архитектура](ARCHITECTURE.ru.md) · [Транспорт USB](USB-TRANSPORT.md) |
| Управлять USB и микшером через API | [API](API.ru.md) |
| Проверить обычный звук Windows | [Системные устройства](ENDPOINTS.ru.md) · [Прежний TAG](GATEWAY.ru.md) |
| Посмотреть прежнюю работу | [Архив AoIP](https://github.com/danrey-bilo/Rey-Audio-Driver-win/blob/18e6353fa767dcbcde1cef15dabab339848e62d1/archive/aoip/README.md) · [Старый установщик](https://github.com/danrey-bilo/Rey-Audio-Driver-win/blob/18e6353fa767dcbcde1cef15dabab339848e62d1/archive/windows-endpoints-2.7/README.md) |

ASIO-пакет устанавливается без Test Mode и нового драйвера ядра. Системные
микрофоны/динамики Windows он не создаёт. Текущие тесты используют цифровую
петлю; ADC/DAC не квалифицирован. Учитывайте это при сравнении расчёта буферов
и измеренного roundtrip.

[Вернуться к проекту](../README.ru.md)

[Обновление интерфейса и очистка Pi](USB-PREVIEW2-20261004.ru.md)
