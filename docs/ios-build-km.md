# Сборка uaRO под iPhone / iPad (iOS / iPadOS)

**Хорошая новость:** SDL3 поддерживает iOS, а bgfx рисует через **Metal** — как и на macOS,
отдельный платформенный бэкенд НЕ нужен, работает наш `platform/sdl`. Плюс наш **тач-режим
(#114)** ровно под iOS, и стартовые ассеты зашиты в бинарник.

## Что уже сделано (в коде, без Mac)

- **Точка входа**: на iOS SDL сам поднимает приложение (UIApplicationMain) и вызывает
  `SDL_main` — в `main.cpp` под `TARGET_OS_IOS` подключается `SDL3/SDL_main.h` (как на Android).
  macOS-десктоп это не задевает.
- **Пресет `ios`** (`CMakePresets.json`): генератор Xcode, `CMAKE_SYSTEM_NAME=iOS`,
  `arm64`, deployment target 16.3 (минимум из-за std::format<float> в логгере; Apple ввёл to_chars<float> в iOS 16.3/macOS 13.3), триплет `arm64-ios`. Шейдеры берутся готовыми из macOS-сборки
  (`CLIENT_PREBUILT_SHADERS`), т.к. iOS — кросс-таргет (target-shaderc не запустить на хосте).
- **Бандл + Info.plist**: `ios/Info.plist.in` — device family iPhone+iPad, ландшафт, `metal`
  в required capabilities, launch screen. iOS всегда собирается как подписанный `.app`.
- **Фреймворки**: под iOS линкуются `Metal/QuartzCore/UIKit/CoreGraphics/CoreMotion/AVFoundation`
  (не Cocoa — это только macOS). AVX2-exe на ARM не собирается (уже общий гейт).
- **Метал-шейдеры**: профиль `metal` уже собирается на APPLE; iOS их переиспользует.

## Требования

- **Физический Mac + Xcode** (iOS SDK/линкер/подпись есть только на macOS — как и macOS-сборка,
  кросс-компилить с Windows/Linux нельзя).
- **Apple Developer аккаунт**: бесплатного хватает, чтобы запустить на СВОЁМ устройстве
  (7-дневная подпись); для TestFlight/публичного теста — платный ($99/год) + App Store Connect.
- **vcpkg** соберёт sdl3/bgfx/… под `arm64-ios`.

## Сборка (на Mac)

```bash
# 1) сначала macOS-сборка -- она даёт готовые metal-шейдеры для iOS
cmake --preset mac-clang-release && cmake --build --preset mac-clang-release

# 2) сгенерировать Xcode-проект под iOS
export VCPKG_ROOT=~/vcpkg
cmake --preset ios -DCMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM=<TEAMID>
# открыть build/ios/*.xcodeproj в Xcode, выбрать устройство, Run;
# либо из консоли:
cmake --build build/ios --config Release -- -allowProvisioningUpdates
```
`<TEAMID>` — Team ID из Apple Developer (Membership). Без него Xcode не подпишет для устройства.

## Осталось (итерации на реальном устройстве)

- Первый прогон: bgfx поднимает Metal, SDL создаёт `CAMetalLayer`-вью, тач-режим включается.
- **Контент (GRF)**: iOS-приложение в песочнице — GRF не положить «рядом с exe». Варианты:
  (а) класть в бандл (Resources) и монтировать оттуда, (б) патчер качает в `Documents/`
  (у нас патчер уже пишет в `pref_dir`, на iOS это и есть Documents). Стартовый UI работает
  и без GRF (ассеты зашиты).
- Иконки: набор `AppIcon` (Assets.xcassets) + launch screen при желании.
- Ориентация/safe-area: проверить вырезы (notch/Dynamic Island) — тач-раскладка уже адаптивная.
- Симулятор: на Apple Silicon Mac iOS Simulator поддерживает Metal; для него нужен триплет
  `arm64-ios-simulator` (отдельная сборка) — но проще гонять на реальном устройстве.

## Важно

Нужен Mac. Весь код кросс-платформенный: iOS-ветки под `TARGET_OS_IOS` / `if(IOS)` не задевают
macOS/Linux/Windows/Android.
