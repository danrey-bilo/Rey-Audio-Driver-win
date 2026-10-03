[English](README.md) · **Русский**

# Документация Rey Audio Driver

Rey Audio Driver 2.8 — предварительная USB-ASIO версия для одной Raspberry
Pi 5 и аудио 8×8. Начните с установки, затем настройте микшер и ручные буферы.

| Задача | Документ |
|---|---|
| Скачать установщик Windows | [Релиз USB preview](https://github.com/danrey-bilo/Rey-Audio-Driver-win/releases/tag/v2.8.0-usb-preview.1) |
| Установить и выбрать драйвер в Ableton | [USB-ASIO](USB-ASIO.ru.md) |
| Настроить блок, запас ASIO и очередь USB | [Буферы](BUFFER-GUIDE.ru.md) |
| Управлять каналами и индикаторами | [Микшер](MIXER.ru.md) |
| Посмотреть измерения и ограничения | [Проверка 2.8](USB-ONLY-2.8.ru.md) · [Исходные данные](evidence/rey-usb-only-20261003.json) |
| Узнать изменения релиза | [Описание релиза](RELEASE-2.8.0.ru.md) |
| Собрать ПО и установщик | [Сборка](BUILD.ru.md) · [Внешний ASIO SDK](ASIO-SDK.ru.md) |
| Разобраться в аудиотракте | [Архитектура](ARCHITECTURE.ru.md) · [Транспорт USB](USB-TRANSPORT.md) |
| Управлять USB и микшером через API | [API](API.ru.md) |
| Проверить обычный звук Windows | [Системные устройства](ENDPOINTS.ru.md) · [Прежний TAG](GATEWAY.ru.md) |
| Посмотреть прежнюю работу | [Архив AoIP](../archive/aoip/README.md) · [Старый установщик](../archive/windows-endpoints-2.7/README.md) |

ASIO-пакет устанавливается без Test Mode и нового драйвера ядра. Системные
микрофоны/динамики Windows он не создаёт. Текущие тесты используют цифровую
петлю; ADC/DAC не квалифицирован. Учитывайте это при сравнении расчёта буферов
и измеренного roundtrip.

[Вернуться к проекту](../README.ru.md)
