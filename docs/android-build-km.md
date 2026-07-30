# Сборка uaRO под Android (arm64-v8a + x86_64)

> APK собирается сразу под две ABI: **arm64-v8a** (телефоны/планшеты) и **x86_64** (Chromebook на
> Intel/AMD через ARCVM + эмуляторы Android Studio/LDPlayer). Триплет vcpkg выбирается по `ANDROID_ABI`
> в `android/jni/CMakeLists.txt` (`arm64-v8a → arm64-android`, `x86_64 → x64-android`) — в gradle он
> НЕ зашит, иначе обе ABI собрались бы с зависимостями одной архитектуры. vcpkg соберёт зависимости
> отдельно под каждый триплет (первый прогон под x64-android — долгий). Чтобы собрать одну ABI, сузь
> `abiFilters` в `app/build.gradle`.

Каркас Android-порта. Клиент собирается в `libmain.so`, который загружает SDL-активность.
Стартовый UI (шрифты, переводы, шейдеры, `sclientinfo.xml`) **зашит в бинарник**, поэтому клиент
запускается и подключается к серверу без внешних файлов; GRF'ы патчер докачивает во внутреннее
хранилище приложения.

## Что уже сделано (в коде)

- **essl-шейдеры (GLES3)**: профиль `essl|android|300_es` добавляется при `ANDROID`; `shader_profile_dir()`
  на Android возвращает `essl`. Шейдеры встраиваются в `.so` (в т.ч. essl), внешняя папка не нужна.
- **Entry point**: на Android `main()` переименовывается в `SDL_main` через `<SDL3/SDL_main.h>`;
  `SDL_MAIN_HANDLED`/`SDL_SetMainReady` на Android не используются (точкой входа владеет SDL-активность).
- **CMake-таргет**: при `ANDROID` вместо exe собирается `add_library(UaRO SHARED …)` → `libmain.so`
  (`OUTPUT_NAME "main"`); AVX2-exe отключён.
- **dataDir**: на Android — `SDL_GetPrefPath` (внутреннее хранилище приложения), т.к. cwd нет.
- **Каркас проекта**: `android/` (gradle + манифест + `MainActivity extends SDLActivity` + `jni/CMakeLists.txt`).

## Требования

- **Android Studio** + **NDK** (проверено под `26.3.x`; поправь `ndkVersion` в `app/build.gradle`).
- **vcpkg** с триплетами `arm64-android` **и** `x64-android` (соберёт sdl3, bgfx, harfbuzz, zlib, curl,
  libvorbis, zstd под каждую ABI; выбор триплета — автоматом по `ANDROID_ABI`).
- **SDL3 Java-сорсы** (`org.libsdl.app.*`): **уже завендорены** в `android/app/sdl3-java/org/libsdl/app/`
  (из libsdl-org/SDL, zlib-лицензия) — это дефолтный srcDir в build.gradle, ничего копировать не надо.
  Если vcpkg соберёт libSDL3.so другой версии SDL3 и будет рассинхрон native↔java — перекрой env
  `SDL3_ANDROID_JAVA` путём к совпадающему дереву.

## Шаги сборки

1. Установи NDK через Android Studio (SDK Manager → NDK).
2. Задай окружение:
   - `VCPKG_ROOT` → путь к vcpkg (или `vcpkgRoot` в `android/gradle.properties`).
   - `SDL3_ANDROID_JAVA` → путь к Java-сорсам SDL3 (см. выше).
3. Из `android/`:
   ```
   ./gradlew assembleDebug
   ```
   Gradle через externalNativeBuild вызовет CMake с цепочкой тулчейнов
   (`CMAKE_TOOLCHAIN_FILE=vcpkg.cmake` + `VCPKG_CHAINLOAD_TOOLCHAIN_FILE=<NDK>/android.toolchain.cmake`),
   vcpkg подтянет зависимости под каждую ABI (`arm64-android` + `x64-android`), соберётся `libmain.so`
   на каждую, всё упакуется в APK.
4. APK: `android/app/build/outputs/apk/debug/app-debug.apk` → установка `adb install`.

## Осталось доделать (следующие итерации на устройстве/эмуляторе)

- Первый прогон: убедиться, что SDL создаёт GLES3-контекст и bgfx поднимается на `OpenGLES`.
- Патчер: путь загрузки GRF в `getExternalFilesDir` (сейчас dataDir = внутреннее хранилище; для больших
  GRF лучше внешнее — `SDL_GetAndroidExternalStoragePath`).
- Иконка приложения (`res/mipmap`), splash, `versionName`.
- Тач-режим уже реализован (см. touchscreen mode) — проверить на реальном мультитаче.
- Возможно: линковать SDL3 статически в `libmain.so` (тогда убрать `"SDL3"` из `getLibraries()`).

## Примечание

Собрать/проверить это можно только с установленным NDK — в CI/деске Android-таргет не участвует
(всё под `if(ANDROID)`/`#ifdef __ANDROID__`, десктоп-сборка не меняется).
