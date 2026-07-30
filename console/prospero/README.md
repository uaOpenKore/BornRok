# `console/prospero/` — слот бэкенда PlayStation (PS5 «Prospero» / PS4 «Orbis»)

Каркас PlayStation-порта, доведённый до состояния **«осталось вписать вызовы SDK»**. Файлы
здесь **НЕ в CMake** и **не содержат ни одного проприетарного заголовка Sony** — не
собираются как есть. Каждое место с SDK помечено `// TODO(SDK):`. Всё остальное (стыковка
с движком) — финально и совпадает с бэкендами SDL/WinRT/NX.

PS5 и PS4 используют **одни и те же семейства API Sony** (scePad, sceUserService,
sceSaveData, sceVideoOut, sceSystemService) с погенерационными отличиями, поэтому один слот
покрывает обе консоли; расхождения помечены в комментариях. Реальный код с NDA-заголовками
пишется на **лицензированной машине** и в публичную часть репо **не коммитится**.

## Модель интеграции (как у Xbox/WinRT и NX)

Сборка **подменяет TU платформы**: десктоп компилит `src/platform/sdl/SdlPlatform.cpp`,
PS — вместо него `console/prospero/PsPlatform.cpp`. Движок (`app/`, `game/`, `render/`) не меняется.

## Файлы (готовый каркас, вписать SDK)

- **`PsPlatform.cpp`** — `uaro::Window` + `uaro::Platform` + `uaro::fs::*` на Sony API:
  - `sceVideoOut` — поверхность вывода → `bgfx::PlatformData.nwh` (рендер = **AGC** на PS5 /
    **GNM** на PS4, исходники bgfx-бэкенда под NDA).
  - `scePad` — DualSense/DualShock 4 → `InputState::Gamepad`, тип `PadType::PlayStation`.
    **Кнопки ПО ПОЗИЦИИ**: Cross(низ)→south, Circle(право)→east, Square(лево)→west,
    Triangle(верх)→north (см. `docs/gamepad-buttons.md`). Стики 0..255 → [-1,1], радиальная
    мёртвая зона как в SdlPlatform, инверсия Y. Тачпад DualSense → уже готовые поля
    `Gamepad.touchActive/touchX/touchY/touchPress` (тачпад-как-мышь, #102).
  - `sceSystemService` — события жизненного цикла → `onLifecycle` (suspend/resume).
  - Язык системы → 2-буквенный ISO.
- **`PsConsoleServices.{hpp,cpp}`** — `ConsoleServices` на `sceSaveData` (mount/unmount каталога
  save-data; unmount = коммит, ОС показывает иконку сохранения) + `sceUserService` (initial user +
  ник). Конфиг/хотбар уже ходят через `consoleServices().saveWrite/Read` под `CLIENT_CONSOLE` —
  заполнить три метода, и персист работает без правок игрового кода.
- **`PsMain.cpp`** — стандартный `main()`: ставит `PsConsoleServices`, монтит save-data, собирает
  `AppConfig` (патчер выключен — ассеты в титуле) и зовёт тот же `Application::run()`.

## Что подключает лицензиат на своей SDK-машине

1. bgfx **AGC** (PS5) / **GNM** (PS4)-бэкенд (NDA) — `RenderDevice` даёт ему `nwh` из `Window::native()`.
2. SDK-тулчейн Sony (PS5 SDK / PS4 SDK) + линковка с `sce*`.
3. CMake: пресет `prospero`/`orbis` (по образцу `xbox-uwp`), компилит `console/prospero/*.cpp`
   вместо `sdl/`, исключает SDL/bgfx[tools], задаёт `CLIENT_CONSOLE_BACKEND=prospero`
   (→ дефайн `CLIENT_CONSOLE` для save-роутинга), шейдеры под консольный профиль bgfx.
4. Спека save-data (размер/`titleId`), иконки/TRC-требования, сертификация (Sony QA).

## В нашу пользу уже готово

Геймпад-UX (#115) с полями тачпада, зашитые стартовые ассеты, lifecycle-пауза кадра/аудио
в `Application::run`. Тач основного экрана (#114) — не для PS (только геймпад + тачпад пада).
