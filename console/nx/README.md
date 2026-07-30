# `console/nx/` — слот бэкенда Nintendo Switch (NX)

Каркас Switch-порта, доведённый до состояния **«осталось вписать вызовы SDK»**. Файлы
здесь **НЕ в CMake** и **не содержат ни одного проприетарного заголовка Nintendo** — они
не собираются как есть. Каждое место, где нужен SDK, помечено `// TODO(SDK):`. Всё
остальное (как слот стыкуется с движком) — финально и совпадает с бэкендами SDL/WinRT.

Реальный NX-код с NDA-заголовками пишется на **лицензированной машине** и в публичную
часть репо **не коммитится** — сюда идут только чистые интерфейсы/заглушки.

## Модель интеграции (как у Xbox/WinRT)

Сборка **подменяет TU платформы**: десктоп компилит `src/platform/sdl/SdlPlatform.cpp`,
NX — вместо него `console/nx/NxPlatform.cpp`. Движок (`app/`, `game/`, `render/`) не
меняется — `Application` владеет конкретным `uaro::Platform` и зовёт его методы.

## Файлы (готовый каркас, вписать SDK)

- **`NxPlatform.cpp`** — реализация `uaro::Window` + `uaro::Platform` + `uaro::fs::*` на
  NX. Что вписать:
  - `nn::vi` — дисплей/слой → нативное окно для `bgfx::PlatformData.nwh` (рендер = **NVN**, NDA).
  - `nn::hid` — Npad (Joy-Con/Pro/Handheld) → `InputState::Gamepad`, тип `PadType::Nintendo`.
    **Кнопки мапить ПО ПОЗИЦИИ** (низ/право/лево/верх), не по подписи — у Nintendo A/B
    зеркальны относительно Xbox; так бинды одинаковы на всех геймпадах (см. `docs/gamepad-buttons.md`).
  - Стики: `AnalogStickState` [-32768..32767] → [-1,1], радиальная мёртвая зона как в SdlPlatform,
    **инверсия Y** (вверх = +y).
  - Портатив: `nn::hid` тач-скрин → `InputState::touch` (тач-режим #114 уже готов; `touch.present`
    включает авто-режим).
  - `nn::settings::GetLanguageCode()` → 2-буквенный ISO для авто-языка.
- **`NxConsoleServices.{hpp,cpp}`** — `ConsoleServices` на `nn::fs` (контейнеры save-data) +
  `nn::account` (юзер/ник) + `nn::oe` (сон/фокус). Движок уже гоняет `settings/game.cfg` и хотбар
  через `consoleServices().saveWrite/Read` (под `CLIENT_CONSOLE`), так что заполнить три метода —
  и персист конфига/хотбара работает без правок игрового кода. **После каждой записи обязателен
  `nn::fs::Commit`**, иначе данные теряются при выключении.
- **`NxMain.cpp`** — вход `nnMain()`: ставит `NxConsoleServices`, монтирует save-data, собирает
  `AppConfig` (патчер выключен — ассеты зашиты в титул) и зовёт тот же `Application::run()`.

## Что подключает лицензиат на своей SDK-машине

1. bgfx **NVN**-бэкенд (NDA) — активировать/слинковать; `RenderDevice` даёт ему `nwh` из `Window::native()`.
2. NDK-тулчейн (clang AArch64) + линковка с `nn::*`.
3. CMake: добавить пресет `nx` (по образцу `xbox-uwp`), который компилит `console/nx/*.cpp` вместо
   `sdl/`, исключает SDL/bgfx[tools], задаёт `CLIENT_CONSOLE_BACKEND=nx` (→ дефайн `CLIENT_CONSOLE`
   для save-роутинга), шейдеры — хостовым shaderc (`CLIENT_HOST_SHADERC`) в профиль NVN.
4. NMETA/спека save-data (размер контейнера), иконка, возрастной рейтинг, прогон lot-check.

## В нашу пользу уже готово

Геймпад-UX (#115), тач для портатива (#114), зашитые стартовые ассеты, консервативные
рендер-пресеты (Switch жёстче всех по ресурсам — держать дефолты как для слабого Android,
render-scale/FSR из видео-настроек), lifecycle-пауза кадра/аудио уже в `Application::run`.
