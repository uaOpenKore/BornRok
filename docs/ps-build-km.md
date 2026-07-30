# Сборка uaRO под PlayStation (PS5 «Prospero» / PS4 «Orbis»)

> ⚠️ **Требуется официальная лицензия Sony.** PS5/PS4 SDK, тулчейн и `bgfx`-бэкенды **AGC** (PS5) /
> **GNM** (PS4) распространяются **только по NDA** (регистрация на
> [partners.playstation.net](https://partners.playstation.net)). В этом репозитории **нет ни одного
> проприетарного заголовка Sony** — только наши интерфейсы и заглушки. Документ описывает, что делает
> **лицензированный разработчик** на своей SDK-машине, чтобы собрать тестовый билд для дев-кита.

Один слот `console/prospero/` покрывает обе консоли — у PS5/PS4 общие семейства API Sony
(`scePad`, `sceUserService`, `sceSaveData`, `sceVideoOut`, `sceSystemService`); отличается только
рендер-бэкенд и SDK-тулчейн. Движок (`app/`, `game/`, `render/`) не меняется (свап TU на уровне CMake).

## Что уже готово (в репозитории)

- `console/prospero/PsPlatform.cpp` — `uaro::Window` + `uaro::Platform` + `uaro::fs::*` на Sony API:
  `sceVideoOut` (поверхность → `bgfx` nwh), `scePad` DualSense/DS4 → `InputState::Gamepad` (маппинг ПО
  ПОЗИЦИИ: Cross→низ, Circle→право, Square→лево, Triangle→верх, тип `PlayStation`), тачпад пада → уже
  готовые поля `Gamepad.touch*`, `sceSystemService` → жизненный цикл, язык системы.
- `console/prospero/PsConsoleServices.{hpp,cpp}` — `ConsoleServices` на `sceSaveData` (mount/unmount =
  коммит) + `sceUserService`. Конфиг/хотбар уже ходят через `consoleServices().saveWrite/Read` —
  заполнить 3 метода, и персист работает без правок игрового кода.
- `console/prospero/PsMain.cpp` — обычный `main()`: ставит сервисы, монтит save, зовёт тот же
  `Application::run()` (патчер выключен — ассеты в титуле).
- CMake: `CLIENT_CONSOLE_BACKEND=prospero` (PS5) или `orbis` (PS4) → определяет `CLIENT_PROSPERO`/
  `CLIENT_ORBIS` + `CLIENT_CONSOLE`, подменяет платформенный слой на `console/prospero/`, отключает SDL,
  аудио — заглушка, AVX2-exe/host-утилиты/тесты пропускаются. `RenderDevice`: `CLIENT_PROSPERO` →
  `RendererType::Agc`, `CLIENT_ORBIS` → `RendererType::Gnm`.

## Шаг 1. Заполнить точки SDK

В трёх файлах `console/prospero/` заполнить **каждый** `// TODO(SDK):` вызовами `sce*` из SDK (поставить
соответствующие `#include`). Объём — склейка: `sceVideoOutOpen`, `scePadReadState`, `sceSaveDataMount`.
Все места и точный смысл — в комментариях рядом с TODO.

## Шаг 2. Что подготовить на SDK-машине

1. **PS5 SDK** (или **PS4 SDK**) установлен, тулчейн настроен, есть **CMake toolchain file** от SDK.
2. **`bgfx` с бэкендом AGC** (PS5) / **GNM** (PS4) собран (исходники под NDA) и находится через
   `find_package(bgfx)` — линкуется в `client_render`.
3. **Хостовый `shaderc`** (из десктопной сборки) — для компиляции шейдеров в консольный профиль bgfx.
4. **Spec save-data** (`titleId`, размер), иконки, TRC-требования — из шаблонов SDK.

## Шаг 3. Переменные окружения + сборка

```sh
# PS5:
export UARO_PROSPERO_TOOLCHAIN=/path/to/prospero/sdk/toolchain.cmake
export UARO_HOST_SHADERC=/path/to/host/shaderc
cd Client
cmake --preset prospero
cmake --build --preset prospero

# PS4 (тот же слот, GNM):
export UARO_ORBIS_TOOLCHAIN=/path/to/orbis/sdk/toolchain.cmake
cmake --preset orbis
cmake --build --preset orbis
```

Упаковку (`.pkg`) и заливку на дев-кит делают штатными средствами SDK (Pub Tools / Neighborhood) — это
вне CMake, по инструкции Sony.

## Заметки

Геймпад-UX (#115) уже с полями тачпада DualSense. Пауза кадра/аудио на suspend встроена в
`Application::run` через `ConsoleServices::onLifecycle`. Тач основного экрана (#114) для PS не нужен —
только геймпад + тачпад пада.
