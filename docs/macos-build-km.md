# Сборка uaRO под macOS

**Хорошая новость:** SDL3 поддерживает macOS (в отличие от WinRT/Xbox), поэтому отдельный
платформенный бэкенд НЕ нужен — наш `platform/sdl` работает как на Linux, только рендер идёт через
**Metal** (bgfx выбирает Metal-бэкенд автоматически). Порт максимально близок к десктопному.

## Что уже сделано (в коде, без Mac)

- **Metal-профиль шейдеров**: `metal|osx|metal|metal` добавляется при `APPLE`; `shader_profile_dir()`
  уже мапит `RendererType::Metal` → `metal`. Metal-шейдеры также попадают в список встраивания в бинарник.
- **CMake-пресеты** `mac-clang` / `mac-clang-release` в `CMakePresets.json` (vcpkg-тулчейн, deployment
  target 13.3, минимум из-за std::format<float>). Статическая линковка libstdc++ на Apple не применяется (там она и не нужна).
- Точка входа: на macOS SDL сам поднимает Cocoa-приложение через `SDL_Init` — наш `main()` +
  `SDL_MAIN_HANDLED`/`SDL_SetMainReady` (десктопный путь) работает как на Linux.

## Требования

- **Xcode Command Line Tools** (`xcode-select --install`).
- **vcpkg** (соберёт sdl3, bgfx, harfbuzz, zlib, curl, libvorbis, zstd под arm64-osx / x64-osx).
- CMake 3.24+.

## Сборка

```
export VCPKG_ROOT=/path/to/vcpkg
cmake --preset mac-clang
cmake --build --preset mac-clang
```
Бинарник и рядом скопированные `shaders/`, `fonts/`, `texts/` — в `build/mac-clang/src/`. (Ассеты
также зашиты в бинарник, так что exe самодостаточен.)

## Что добавлено для Mac (проактивно, под `if(APPLE)`)

- **Авто-линковка фреймворков**: `UaRO` под APPLE линкует `Metal / QuartzCore / Cocoa / IOKit /
  CoreVideo` явно — чтобы даже урезанная vcpkg-сборка не падала на линковке
  (`Undefined symbols ... CAMetalLayer`). Другие платформы не задеты.
- **AVX2-exe отключён на macOS**: на Mac шипим один бинарник (в идеале universal), второй
  `uaro_client-avx2` не собирается (это Windows/Linux-оптимизация).
- **Опциональный `.app`-бандл**: `-DCLIENT_MACOS_BUNDLE=ON` включает `MACOSX_BUNDLE` +
  `macos/Info.plist.in` (CFBundle*, Metal, Retina). По умолчанию **OFF** — обычная mac-сборка
  это простой exe, который находит ассеты рядом (как Linux); ассеты к тому же зашиты в бинарник.

## Осталось (итерации на реальном Mac)

- Первый прогон: убедиться, что bgfx поднимает Metal и SDL создаёт Metal-совместимое окно.
- **Universal-бинарник** (arm64 + x86_64): добавить `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"`
  к конфигурации (нужны universal-зависимости vcpkg; проще собрать нативно под свою арх).
- `.app`-бандл: иконка `macos/uaro.icns` + при распространении — подпись/нотаризация
  (Apple Developer аккаунт). Проверить, что при бандле ассеты/GRF ложатся туда, где их найдёт
  `base_dir()` (SDL_GetBasePath) — на бандле это `Contents/Resources/`, не `MacOS/`.

## Важно

Нужен физический Mac с Xcode для реальной сборки (Apple SDK/линкер только на macOS). Весь код —
кросс-платформенный (`if(APPLE)`/`#ifdef __APPLE__` не задевают Linux/Windows/Android).
