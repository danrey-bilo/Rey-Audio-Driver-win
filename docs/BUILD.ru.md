# Сборка Windows-версии только для USB

Нужны Windows x64, C++17, CMake 3.20+, Windows SDK и .NET Framework 4.8 для
WPF/EXE. ASIO SDK предоставляется отдельно; заголовки не входят в репозиторий
или пакет. Требуется исходный [Pi5-AUSB](https://github.com/danrey-bilo/Pi5-AUSB)
в закреплённом подмодуле `lib/pi5-ausb` (нужен доступ к репозиторию)
или совместимый пакет 0.2. AoIP-lib больше не требуется.

[Команды CMake и сборки EXE](BUILD.md). По умолчанию включены
`REY_BUILD_USB_ASIO`, `REY_BUILD_CONTROL`, `REY_BUILD_TESTS`. Старые флаги
`PIAOIP_BUILD_ASIO`, `PIAOIP_BUILD_SERVICE`, `PIAOIP_ENABLE_USB` больше не
управляют сборкой. Используйте новый каталог сборки, чтобы не переносить
старые зависимости из CMakeCache.

Установщик: `build/usb-setup/Rey-Audio-USB-ASIO-Setup-x64.exe`.
`SHA256.json` содержит контрольные суммы четырёх встроенных бинарников и EXE.
INI, SYS/CAT, SDK и правила брандмауэра в USB-пакет не включаются.
Проверить установленный пакет из PowerShell x64:
`tools/verify_usb_install.ps1 -SetupDirectory build/usb-setup -Output build/install-check.json`.

Проверяются 37 контрактов: IPC, timeline, перенос настроек, PCM, микшер,
отображение каналов и правило одной карты. Реальные проверки выполняются
через установленный COM/ASIO и службу; ASIO в DAW должен быть закрыт.
Один аудиопрогон ограничен 175 секундами, с завершением до трёх минут.
[Текущий отчёт](USB-LATENCY-2026-10-16.md). Панель отдельно проходит 23 проверки C#.

TAG (`REY_ENABLE_TAG_BRIDGE=ON`) и ACX — отдельные эксперименты обычных
аудиоустройств Windows. Они не являются зависимостями USB-ASIO установщика.
Windows ARM64 не поддерживается. Успешная сборка не доказывает задержку ADC/DAC.

## Архивы релиза

После коммита проверенного кода:

```powershell
python tools/package_usb_release.py --setup build/usb-setup --out dist/usb-release `
  --asio-notice C:/SDK/asio/LICENSE.txt --ref HEAD --version 2.8.2-akm-dev.4
```

Скрипт проверяет hash EXE и манифест одной USB-карты, архивирует только файлы
коммита и создаёт контрольные суммы. Внешние SDK/бинарники не включаются.
Установку и публикацию скрипт не выполняет. Закрытые исходники USB-зависимости
не экспортируются Windows-упаковщиком: для сборки отдельно получите закреплённый
`lib/pi5-ausb`. Тестовая сборка `REY_FINITE_QUALIFICATION=ON` изолирована от
установленной службы и не допускается к регистрации ASIO или упаковке установщика.
