# Сборка uaRO под Xbox (UWP / Dev Mode) — все серии

Один UWP-пакет покрывает **все консоли Xbox** (One / One S/X / Series S / Series X): консоль в
**режиме разработчика** запускает обычные UWP-приложения, собранные публичным Windows SDK. **Никакого
проприетарного GDK и NDA** здесь не нужно — это единственный путь, который собирается и тестируется без
лицензий Microsoft. Полноценный релиз в Store через GDK — отдельная задача (нужен аккаунт ID@Xbox).

Собирается **только на Windows** (Windows SDK + C++/WinRT). На Linux WinRT не компилируется —
десктоп/Android/иOS-сборки этот таргет не затрагивают (всё изолировано в `platform/winrt/` и `uwp/`).

## Что реализовано

- `src/platform/winrt/WinrtPlatform.cpp` — нативный WinRT-бэкенд `Platform`/`Window` вместо SDL:
  CoreWindow + ввод (клавиатура/указатель/колесо, RO Alt-хоткеи, хотбар) + геймпад через
  `Windows.Gaming.Input`. Это та же конкретная реализация `Platform::*`/`Window::*`, что линкует движок
  (свап на уровне CMake, без vtable) — `app/`+`game/` не меняются.
- `src/platform/winrt/WinrtConsoleServices.cpp` — сохранения в `ApplicationData.LocalFolder` +
  жизненный цикл (Suspending → пауза кадра, Resuming → продолжение).
- `uwp/App.cpp` — точка входа `IFrameworkView`; ставит WinRT-сервисы и вызывает `Application::run()`.
  Игровой цикл НЕ переписан: `while (platform_.pump(input_))` теперь качает CoreWindow-диспетчер.
- Рендер: `RenderDevice` под `CLIENT_UWP` берёт `RendererType::Direct3D12` (SwapChainForCoreWindow),
  CoreWindow IUnknown\* как `nwh`.
- CMake-таргет выбирается `CLIENT_CONSOLE_BACKEND=xbox` (пресет `xbox-uwp`): подменяет платформенный
  слой на WinRT, отключает SDL, аудио — через XAudio2, второй AVX2-exe не собирается.

## Требования (Windows-машина)

1. **Visual Studio 2022** с рабочими нагрузками:
   - «Разработка приложений для универсальной платформы Windows (UWP)»
   - «Разработка классических приложений на C++»
   - компоненты **C++ (v143) UWP tools** и **Windows 10/11 SDK** (10.0.19041 или новее).
2. **vcpkg** (переменная окружения `VCPKG_ROOT`).
3. Один раз собрать десктопный `win-msvc` — из него берётся хостовый `shaderc.exe` для компиляции
   шейдеров (пресет `xbox-uwp` ссылается через `CLIENT_HOST_SHADERC`). Если десктопной сборки нет —
   сначала `cmake --preset win-msvc` (достаточно сконфигурировать + собрать зависимости).

## Сборка

```bat
cd Client
cmake --preset xbox-uwp
cmake --build --preset xbox-uwp
```

vcpkg подтянет зависимости под триплет `x64-uwp` (bgfx без `tools` — shaderc берём хостовый). Выход —
UWP-приложение `UaRO.exe` + пакет/layout в `build/xbox-uwp`.

## Деплой на консоль (Dev Mode)

1. На Xbox установить **Dev Mode Activation** (Microsoft Store) и активировать режим разработчика
   (разовый Dev-аккаунт ~19$ через Partner Center). Консоль перезагрузится в Dev Home.
2. В Dev Home включить **Device Portal** (адрес вида `https://<ip>:11443`).
3. Из Visual Studio: свойства проекта `UaRO` → Debugging → Remote Machine = IP консоли; либо загрузить
   пакет через Device Portal (Add → `.appx` + зависимости).
4. Запустить из раздела приложений режима разработчика.

Управление — геймпадом (`docs/gamepad-buttons.md`), позиционные кнопки (A=низ/подтвердить, B=право/отмена).

## Ограничения первого запуска (снимаем по мере доводки)

- **Патчер выключен** (`cfg.noPatch`) + включён `testMode`: сеть/докачка на UWP (libcurl/WinHTTP) —
  отдельная задача. Ассеты пока встроены/кладём в LocalFolder.
- **Звук — есть (XAudio2)**: SDL-аудио под UWP нет, поэтому на Xbox/UWP `client_audio` собирается с
  `CLIENT_AUDIO_XAUDIO2` (вместо `CLIENT_WITH_AUDIO`) и играет через встроенный XAudio2 (xaudio2_9):
  одна mastering-voice + по source-voice на SFX/эмбиент/BGM, ресемпл+микс делает сам XAudio2, BGM
  зациклен через `XAUDIO2_LOOP_INFINITE`. Декод тот же (dr_wav/dr_mp3/dr_flac). RoM-FSB SFX только на
  десктопе. Отдельной capability для воспроизведения не нужно; BGM/wav-паки уже запечены в appx.
- **Сохранения** идут в `LocalFolder`; XUser/XGameSave — уже под GDK/лицензией.
- Первая компиляция на Windows почти наверняка потребует пары правок (WinRT чувствителен к версии
  SDK/заголовкам) — это ожидаемо, правим по логу.

## TODO к «настоящему» Xbox (GDK)

- Аккаунт ID@Xbox + лицензионный GDK (NDA) → полный «игровой» режим, релиз в Store.
- `WinrtConsoleServices`: `activeUserName`/`onlineAllowed` → XUser; сейвы → XGameSave-контейнеры.
- Патчер/сеть, деплой-ассеты в пакет, реальный Identity/Publisher из Partner Center. (Аудио уже есть —
  XAudio2, см. выше.)
