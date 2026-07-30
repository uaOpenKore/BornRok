# Сборка `uaRO.exe` на Windows в Visual Studio Code

Клиент — обычный CMake-проект с пресетами (`Client/CMakePresets.json`). VS Code
собирает его через расширение **CMake Tools**. Ниже — с нуля.

## 1. Поставить один раз (prerequisites)

1. **Visual Studio 2022** (Community) или **Build Tools for VS 2022** — при установке
   выбрать рабочую нагрузку **«Разработка классических приложений на C++»**
   (даёт компилятор MSVC `cl.exe` и Windows SDK).
2. **CMake ≥ 3.24** — идёт в комплекте с VS, либо поставить отдельно и добавить в PATH
   (`cmake --version`).
3. **vcpkg** — менеджер зависимостей (SDL3/bgfx/zlib/curl/zstd — список в vcpkg.json, ставится сам при конфигурации):
   ```bat
   git clone https://github.com/microsoft/vcpkg C:\vcpkg
   C:\vcpkg\bootstrap-vcpkg.bat
   ```
   Затем задать переменную окружения **`VCPKG_ROOT=C:\vcpkg`** (через «Изменение
   системных переменных среды» или `setx VCPKG_ROOT C:\vcpkg`, после — перезапустить
   VS Code). Пресеты ссылаются на `$env{VCPKG_ROOT}` для toolchain-файла.
4. **VS Code** + расширения (при открытии папки VS Code предложит их сам — см.
   `.vscode/extensions.json`):
   - **C/C++** (`ms-vscode.cpptools`)
   - **CMake Tools** (`ms-vscode.cmake-tools`)

## 2. Открыть проект

**File → Open Folder…** и выбрать папку **`...\uAthena\Client`** (именно `Client`,
там лежит `CMakeLists.txt` и `.vscode/`). Согласиться поставить рекомендованные
расширения. CMake Tools подхватит пресеты автоматически.

> Если открываете корень репозитория `uAthena`, поправьте в `.vscode/settings.json`
> `"cmake.sourceDirectory": "${workspaceFolder}/Client"`.

## 3. Сконфигурировать и собрать

В строке состояния снизу (или Command Palette `Ctrl+Shift+P`):

1. **CMake: Select Configure Preset** → **`win-msvc`**.
2. **CMake: Configure** — первый запуск через vcpkg соберёт SDL3 + bgfx + zlib из
   исходников (несколько минут, один раз).
3. **CMake: Build** (или клавиша **F7**) — соберёт **`UaRO.exe`**.

Готовый exe: `Client/build/win-msvc/src/<Config>/UaRO.exe`.

Эквивалент в терминале:
```bat
set VCPKG_ROOT=C:\vcpkg
cmake --preset win-msvc
cmake --build --preset win-msvc
```

### Звук (#103)

Аудио собирается **по умолчанию, без всяких флагов** — опция `CLIENT_WITH_AUDIO` включена (`ON`)
в `src/CMakeLists.txt`, пресет `win-msvc` её не трогает. То есть обычная сборка уже со звуком:
```bat
cmake --preset win-msvc
cmake --build --preset win-msvc
```
Бэкенд — **штатное SDL3 core audio** (SDL3 и так нужен клиенту) плюс встроенные public-domain
декодеры `dr_wav.h` / `dr_mp3.h` в `src/audio/`. **Никаких доп. зависимостей**: SDL3_mixer не
используется (его новый `MIX_*` API несовместим со старым `Mix_*`), `sdl3-mixer` из `vcpkg.json`
убран — доустанавливать ничего не надо.

BGM (`BGM/NN.mp3`) лежит в корне клиента рядом с `data/`. На Linux называй папку строчными
`bgm/` — клиент намеренно приводит все пути к нижнему регистру.

Звук можно отключить (тихая заглушка) через `-DCLIENT_WITH_AUDIO=OFF`, но по умолчанию он включён.

## 4. Запуск / отладка

- Внизу в CMake Tools выбрать **Launch target → `UaRO`**.
- **F5** или конфигурация **«Run UaRO (Windows)»** (см. `.vscode/launch.json`).
  Рабочая папка ставится в каталог exe, чтобы он нашёл скомпилированные шейдеры
  рядом с собой.

Должно открыться окно с тестовыми спрайтами (три цветных квадрата + бегающий
чекер). Esc — выход.

## 5. Быстрая проверка без зависимостей (опционально)

Не нужен ни SDL/bgfx, ни vcpkg — собирает только `core/` и гоняет юнит-тесты:
```bat
cmake --preset core-only
cmake --build --preset core-only
ctest --preset core-only --output-on-failure
```

## 6. Сборка под Windows on ARM (ARM64)

Кросс-сборка **с обычного x64-хоста** на ARM64 (Windows on ARM). Только статика,
single-file exe — как и десктоп.

**Предусловия:**
1. В **Visual Studio Installer** → «Изменить» → вкладка **«Отдельные компоненты»** →
   поиск `ARM64` → отметить **«MSVC v143 — VS 2022 C++ ARM64/ARM64EC build tools (Latest)»**.
   Без этого MSBuild ругается `Platform='ARM64'` не существует (`VCTargetsPath`).
2. Сначала собрать обычный `win-msvc` (x64) — он даёт **host-shaderc** (arm64-shaderc
   не запускается на x64-хосте, поэтому берётся x64-версия из win-msvc сборки).

```bat
cmake --preset win-msvc
cmake --build --preset win-msvc
cmake --preset win-arm64
cmake --build --preset win-arm64
```
Первый прогон `win-arm64` собирает все vcpkg-зависимости под arm64 из исходников
(долго). Exe: `Client/build/win-arm64/src/Release/UaRO.exe`.

Если host-shaderc лежит не по дефолтному пути — переопредели:
`-DCLIENT_HOST_SHADERC=<путь к x64 shaderc.exe>`.

> На ARM **нет AVX2**, поэтому второй `uaro_client-avx2.exe` под ARM не собирается —
> только базовый `UaRO.exe`.

## Заметки и подводные камни

- **⚠️ Собирай через build-пресет, а не по пути каталога (ошибка `MSB8013` / `Debug|x64`).**
  Windows-пресеты собираются **только в Release** (Debug убран намеренно). VS —
  multi-config генератор: команда `cmake --build build/win-msvc` **без `--config`** по
  умолчанию берёт `Debug`, которого нет → падает с
  `error MSB8013: этот проект не содержит сочетание конфигурации и платформы Debug|x64`.
  Правильно — использовать **build-пресет** (он несёт `Release`):
  ```bat
  cmake --build --preset win-msvc
  ```
  (`--preset win-msvc`, а НЕ `--preset build/win-msvc` и не путь к каталогу.)
  Если хочешь собирать по пути каталога — добавляй `--config Release`:
  ```bat
  cmake --build build/win-msvc --config Release
  ```
- **Шейдеры.** Их компилирует `shaderc` (инструмент bgfx) на этапе сборки. Если в
  vcpkg-сборке bgfx `shaderc` не оказалось в PATH — клиент всё равно запустится, но
  покажет только цвет очистки (в лог пойдёт предупреждение). Поставьте/укажите
  `shaderc`, чтобы спрайты рисовались.
- **Имена таргетов bgfx.** Код рассчитан на vcpkg-порт (`bgfx::bgfx` / `bgfx::bx` /
  `bgfx::bimg`). Если соберёте bgfx через `bgfx.cmake`, поправьте имена в
  `src/CMakeLists.txt`.
- **`VCPKG_ROOT` не задан** → ошибка конфигурации (toolchain-файл не найден). Задайте
  переменную и перезапустите VS Code.
- **Консольное окно.** Сейчас exe собирается как консольное приложение — рядом с
  окном появится консоль с логами. Это нормально для разработки.
- **Смена триплета vcpkg (например, переход на статическую линковку).** Старый кеш
  CMake хранит пути прежнего триплета, и конфигурация падает с ошибкой вида
  «Imported target "ZLIB::ZLIB" includes non-existent path ... x64-windows-static-md».
  Лечение — разово удалить каталог сборки и переконфигурировать:

  ```bat
  rmdir /s /q build\win-msvc
  cmake --preset win-msvc
  cmake --build build/win-msvc --config Release
  ```

  Первая сборка после смены триплета пересобирает все vcpkg-зависимости (20–40 минут),
  дальше сборки снова быстрые.
