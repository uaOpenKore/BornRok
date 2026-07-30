# Сборка uaRO под Nintendo Switch (NX)

> ⚠️ **Требуется официальная лицензия Nintendo.** NDK Switch, тулчейн и `bgfx`-бэкенд **NVN**
> распространяются **только по NDA** через [Nintendo Developer Portal](https://developer.nintendo.com).
> В этом репозитории **нет ни одного проприетарного заголовка Nintendo** — только наши интерфейсы и
> заглушки. Этот документ описывает, что должен сделать **лицензированный разработчик** на своей
> SDK-машине, чтобы собрать тестовый билд для дев-кита.

Сборка изолирована так же, как Xbox: движок (`app/`, `game/`, `render/`) **не меняется** — консоль это
ещё один платформенный бэкенд (свап TU на уровне CMake, как WinRT для Xbox). Десктоп/Android/иOS-сборки
это не затрагивает.

## Что уже готово (в репозитории)

- `console/nx/NxPlatform.cpp` — реализация `uaro::Window` + `uaro::Platform` + `uaro::fs::*` на NX:
  `nn::vi` (нативное окно → `bgfx` nwh), `nn::hid` Npad → `InputState::Gamepad` (маппинг ПО ПОЗИЦИИ,
  тип `Nintendo`), тач портатива → тач-режим (#114), `nn::oe` (сон/фокус) → жизненный цикл,
  `nn::settings` язык.
- `console/nx/NxConsoleServices.{hpp,cpp}` — `ConsoleServices` на `nn::fs` (контейнеры save-data,
  **обязателен `nn::fs::Commit`**) + `nn::account`. Конфиг и хотбар уже ходят через
  `consoleServices().saveWrite/Read` — заполнить 3 метода, и персист работает без правок игрового кода.
- `console/nx/NxMain.cpp` — точка входа `nnMain()`: ставит сервисы, монтит save, зовёт тот же
  `Application::run()` (патчер выключен — ассеты зашиты в титул).
- CMake: `CLIENT_CONSOLE_BACKEND=nx` → определяет `CLIENT_NX` + `CLIENT_CONSOLE`, подменяет платформенный
  слой на `console/nx/`, отключает SDL, аудио — заглушка, второй AVX2-exe не собирается,
  host-утилиты/тесты пропускаются. `RenderDevice` под `CLIENT_NX` берёт `RendererType::Nvn`.

## Шаг 1. Заполнить точки SDK — ГОТОВО

`console/nx/*.cpp` уже заполнены вызовами `nn::vi/hid/oe/fs/settings/account` (коммиты ad850328,
eb36d733). Если обновляешь под другую ревизию SDK — правки там же.

## Шаг 2. Что подготовить на SDK-машине

1. **Nintendo NDK** установлен, тулчейн (clang AArch64) настроен, есть **CMake toolchain file** от NDK.
2. **`bgfx` с бэкендом NVN** собран (исходники bgfx под NDA) и находится через `find_package(bgfx)`.
   Наш `client_render` линкует `bgfx::bgfx`/`bx`/`bimg` — предоставить их install-дерево.
3. **Хостовый `shaderc`** (собранный десктопной сборкой) — для компиляции шейдеров в профиль NVN.
   Достаточно один раз сконфигурировать десктоп: `cmake --preset win-msvc` (или linux) и взять
   `.../tools/bgfx/shaderc`.
4. **NMETA / spec save-data** (размер контейнера), иконка, возрастной рейтинг — из шаблонов NDK.

## Шаг 3. Переменные окружения + сборка

Пресет `nx` (в `CMakePresets.json`) ссылается на тулчейн NDK и хостовый shaderc через переменные:

SDK 9.2.0 **не поставляет** готового CMake-тулчейна, поэтому в репе лежит наш:
`cmake/console-toolchains/nx.toolchain.cmake` (без NDA-контента, все пути — через `NINTENDO_SDK_ROOT`).

```bat
set NINTENDO_SDK_ROOT=C:\...\NintendoSDK
set UARO_NX_TOOLCHAIN=%CD%\cmake\console-toolchains\nx.toolchain.cmake
set UARO_HOST_SHADERC=C:\...\host\shaderc

cd Client
cmake --preset nx
cmake --build --preset nx
```

Тулчейн настроен на компиляцию (clang `aarch64-nintendo-nx-elf` + инклуды `Include` +
`Common/Configs/Targets/NX-NXFP2-a64/Include`; проверка компилятора идёт как статик-либа, поэтому
конфиг проходит без полного рецепта линковки). Финальную ЛИНКОВКУ exe (nn-стартап + порядок
`libnn_*` + линкер-скрипт SDK) и упаковку `.nso`/`.nspd` через MakeNso/MakeMeta допиши в секции LINK
внутри тулчейна — по флагам из сборки рабочего сэмпла SDK.

Готовый исполняемый образ (`UaRO`) собирается тулчейном NDK; упаковку в `.nsp`/authoring для дев-кита
делают штатными средствами NDK (AuthoringTool) — это вне CMake, по инструкции Nintendo.

## Заметки по производительности

Switch жёстче всех по ресурсам. Держать консервативные дефолты (как для слабого Android): render-scale
/ FSR из видео-настроек (#111), MSAA/пост-эффекты умеренно, тач для портатива уже готов. Пауза кадра и
аудио на `Suspended` уже встроена в `Application::run` через `ConsoleServices::onLifecycle`.
