# Rey Audio Driver 2.7.0 preview

- Card selector on Mixer changes the view only. Separate sessions, profiles,
  mixers and scoped commands for up to ten cards; USB/LAN associate by DeviceId.
- Four Windows stereo input/output pairs per 8×8 board, plus full multichannel
  endpoints; per-file kernel ownership and independent stream slots.
- Exact USB path opening, LAN port conflict checks and a bounded port list in
  the existing Private/LocalSubnet firewall rule. Offline profile removal in UI.
- MSI/EXE with unique free local test signing. Test Mode is required; Secure Boot,
  Core Isolation and reboot remain explicit user actions.
- 24 native contracts, 12 finite real-Pi checks, 30s live selection test with one
  offline fixture, WPF render and installer/signature inspection passed.

**Pre-release:** one physical Pi tested; loaded ACX/WASAPI, actual Windows device
names, Chrome/Telegram, simultaneous physical cards, elevated installation and
physical ADC/DAC remain unqualified. USB physical roundtrip <2ms remains a target.
Existing releases and Pi4 are unchanged.

## Русский

- Выбор карты сверху микшера меняет только отображение. Отдельные сессии,
  профили и микшеры, до десяти карт; один DeviceId объединяет USB и LAN.
- В коде драйвера — пары входов/выходов 1/2, 3/4, 5/6, 7/8 и полный 1–8.
  Каждый служебный handle управляет только своей картой.
- Открытие конкретного USB-пути, проверка LAN-портов, удаление офлайн-профиля.
- EXE/MSI с бесплатной локальной подписью; нужен Test Mode.
- 24 нативных теста, 12 проверок с Pi и 30 с проверки выбора микшера пройдены.
  Реальных карт на стенде одна; системный звук и физическая задержка пока
  требуют проверки после загрузки драйвера.

[English installation](INSTALL.md) · [Установка](INSTALL.ru.md) ·
[Пары каналов](ENDPOINTS.ru.md) · [Архитектура и доказательства](REY-AUDIO-2.7.ru.md)
