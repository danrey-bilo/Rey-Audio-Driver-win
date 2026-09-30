![Win11-asio-AoIP 2.4.3](docs/assets/header.svg)

# Win11-asio-AoIP

[English](README.md) | **Русский**

Драйвер ASIO для Windows 11 x64: соединяет DAW с совместимым сервисом Raspberry Pi AoIP через проводной Ethernet.

**[Скачать 2.4.3](https://github.com/danrey-bilo/Win11-asio-AoIP/releases/tag/v2.4.3)** · **[Описание релиза](docs/RELEASE-2.4.3.ru.md)** · **[Проверки](docs/VALIDATION.ru.md)**

## Начало работы

1. Скачайте [PiAoIP-2.4.3-Windows11-x64.msi](https://github.com/danrey-bilo/Win11-asio-AoIP/releases/download/v2.4.3/PiAoIP-2.4.3-Windows11-x64.msi).
2. Закройте ASIO-приложения и PiAoIP в трее, затем запустите MSI.
3. Откройте **PiAoIP Settings → Device → Find and connect** и выберите Pi.
4. Задайте **Sample rate**, **Bit depth**, **ASIO buffer** и **LAN buffer**, затем нажмите **Apply**.
5. Выберите **Pi AoIP** в 64-битной DAW и запустите её аудиодвижок.

Панель, трей и установленная справка работают на английском. **Автоматический подбор буферов удалён.** Буферы задаются вручную; проверяйте их в своём проекте DAW. Число физических каналов приходит от Pi, а **Device → Channels** позволяет выбрать используемые каналы.

![Настройки PiAoIP](docs/assets/settings.png)

MSI устанавливает ASIO DLL, приложение настроек/трея, инструкцию и лицензии. Добавляет регистрацию ASIO и правило UDP 50021 для локальной подсети. Оба ярлыка меню «Пуск» и само приложение получили встроенные значки.
Системные устройства микрофона/динамиков Windows, драйвер ядра и образ Pi в этот пакет не входят.

## Возможности

| Параметр | Поддержка |
|---|---|
| Система | Windows 11 x64; 64-битный ASIO-хост |
| Главный экран | Частота, разрядность, ASIO-буфер, LAN-буфер |
| ASIO-буфер | 16–2048 сэмплов, степени двойки |
| LAN-буфер | 0–2048 сэмплов, вручную |
| Сессии | Один потоковый ASIO-клиент на пару Pi/ПК |
| Аудиопрофили | До 64 каналов в направлении; 44,1–192 кГц; PCM16/24/32 |
| Транспорт | PiAoIP UDP/IPv4 через Ethernet; не AES67 и не Dante |

## Документация

[Установка](docs/INSTALL.ru.md) · [Сборка](docs/BUILD.ru.md) · [API хоста](docs/API.ru.md) · [Архитектура](docs/ARCHITECTURE.ru.md) · [Буферы](docs/BUFFER-GUIDE.ru.md)

Основной язык документации — английский; у актуальных руководств есть русские версии. Версия 2.4.3 — **предварительный выпуск**. Проверка сборки и пакетов не подтверждает физическую работу ADC/DAC или гарантированную задержку. Сервис Pi генерирует и проверяет синтетический PCM; для физической звуковой карты нужен аппаратный аудиобэкенд.

## Компоненты проекта

| Репозиторий | Назначение |
|---|---|
| [AoIP-lib](https://github.com/danrey-bilo/AoIP-lib) | Протокол и переносимые библиотеки |
| [Win11-asio-AoIP](https://github.com/danrey-bilo/Win11-asio-AoIP) | Драйвер ASIO и панель Windows |
| [Pi4-AoIP](https://github.com/danrey-bilo/Pi4-AoIP) | Сервис Raspberry Pi 4 / PREEMPT_RT |
| [Pi5-AoIP](https://github.com/danrey-bilo/Pi5-AoIP) | Сервис Raspberry Pi 5 / PREEMPT_RT |

## Лицензия

Личное некоммерческое использование бесплатно. Для коммерческого использования требуется отдельная платная письменная лицензия. См. [LICENSE](LICENSE) и [русское пояснение](docs/LICENSE-RU.md). У внешнего Steinberg ASIO SDK отдельные условия: [ASIO SDK](docs/ASIO-SDK.ru.md).
