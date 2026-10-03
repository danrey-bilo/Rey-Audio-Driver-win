[English](BUILD.md) | **Русский**

# Сборка Windows-драйвера

Сборка независимой службы без ASIO и первоначальный kernel-проект описаны в [ACX/service](ACX-SERVICE.ru.md). Ниже приведена сборка прежнего ASIO adapter.

Нужны Windows x64, CMake 3.20+, Ninja, LLVM-MinGW x64/UCRT и отдельно полученный [Steinberg ASIO SDK](ASIO-SDK.ru.md). SDK не входит в репозиторий и архив исходников.

```powershell
git clone --recurse-submodules https://github.com/danrey-bilo/Rey-Audio-Driver-win.git
cd Win11-asio-AoIP
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_CXX_COMPILER=C:/Tools/llvm-mingw/bin/x86_64-w64-mingw32-clang++.exe `
  -DCMAKE_RC_COMPILER=C:/Tools/llvm-mingw/bin/x86_64-w64-mingw32-windres.exe `
  -DASIO_SDK_DIR=C:/SDKs/asio
cmake --build build --parallel 2
```

Результат: `build/bin/PiAoipAsio.dll` и `build/bin/PiAoipControl.exe`. EXE загружает DLL из своего каталога. Оба бинарника имеют версию 2.5.0 и встроенные значки. В `src/ui/assets` лежат SVG-исходники и ICO нескольких разрешений. DLL содержит манифест панели; EXE — манифест приложения и ресурс версии.

`external/AoIP-lib` закреплён на совместимой версии 2.5.0. Альтернатива — `-DAOIP_SOURCE_DIR=/path/to/AoIP-lib` или установленный `AoIP 2.5.0` SDK при отсутствии исходников зависимости. AoIP-lib — закрытая зависимость. Для сборки нужен разрешённый доступ к submodule или совместимый SDK. Публичные архивы не содержат закрытую зависимость.

Цели автоподбора буферов в сборке нет. Тесты core/configuration, скрытый рендер UI и сборка MSI находятся в отдельном рабочем репозитории разработчиков. Для установки пользователю нужен [MSI](INSTALL.ru.md): обычная CMake-сборка не регистрирует драйвер.

## MSI

Пакет содержит два бинарника, английскую инструкцию и лицензии. Ярлыки Settings и Installation Guide используют ресурсы 0 и 1 из EXE; список приложений — основной значок. Семейство major upgrade сохранено. Предыдущая версия заменяется, пользовательский INI хранится отдельно.

Не включайте заголовки и архив Steinberg SDK в релиз. Условия проекта и внешнего SDK действуют отдельно: [ASIO SDK](ASIO-SDK.ru.md).

## Проверки

См. [проверки 2.5.0](VALIDATION.ru.md). Сборка DLL не доказывает стабильность конкретного проекта DAW. На целевом ПК проверяйте формат, буферы, ошибки потока и перезапуск хоста. Физическая задержка требует подключённого аппаратного бэкенда.
