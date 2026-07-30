# uaRO Client — v0 «Foundation» — Design Spec

- **Дата:** 2026-06-21
- **Ветка:** `client/foundation` (от `x64`)
- **Статус:** утверждён, в реализации
- **Подпроект:** v0 (этапы 4.1 «стек подсистем» + 4.2 «дерево `Client/src`»)

## 1. Контекст и стратегия

`uAthena` — монорепозиторий «сервер + клиент». Сервер (athena/eAthena, C/C++ в `src/`) собирается GNU Make + GCC. `Client/` сейчас содержит только `winEXE/` — рантайм оригинального Windows-клиента (`uaRO.exe` — легаси-DirectX, обёрнутый dgVoodoo2; в git не лежит, как и GRF). Исходников клиента нет — реимплементацию начинаем с нуля.

**Стратегия (утверждена):** чистая реимплементация на современном C++ с опорой на open-source референсы (roBrowser, OpenKore, rAthena, open-midgard, GRFEditor). Дизассемблирование `uaRO.exe` — только точечно, для недокументированных мест. Протокол берём из публичных `uaOpenKore/uOK*` (есть `tables/recvpackets-uaro.txt` именно под этот клиент).

**Целевые платформы (итог):** Windows 11, Linux (Debian 13), Android 8+, iOS (iPhone 16+). Рендер абстрагируем через **bgfx** (D3D9/11/12, OpenGL, GLES, Metal, Vulkan) — одна библиотека закрывает все цели; легаси-DX-бэкенды вручную не пишем, dgVoodoo не нужен.

## 2. Объём v0

**Цель:** минимальный, но реально собираемый и запускаемый каркас, на который наслаиваются будущие подсистемы.

**Критерий готовности (Definition of Done):**

1. CMake-проект конфигурируется и собирается на Windows (MSVC) и Linux (GCC/Clang) из единых пресетов.
2. Линкуется с SDL3 (окно/ввод/платформа) и bgfx (рендер).
3. Открывается окно, крутится игровой цикл по vsync, экран очищается, **через нашу абстракцию рендера рисуется тестовый спрайт + треугольник** (доказательство, что путь рендера жив).
4. Корректное завершение, базовый лог.
5. Юнит-тесты на тестируемые части `core/` (IO, математика) проходят.
6. Каркас модулей (`core` / `platform` / `render` / `app` + заглушки) на месте, интерфейсы объявлены.

**Вне объёма v0 (YAGNI):** любые форматы RO, сеть, UI-движок, Lua, мобильные тулчейны, ручные DX-бэкенды. Подключаются в следующих подпроектах.

## 3. Архитектура (стек подсистем — этап 4.1)

Слои снизу вверх; каждый зависит только от нижних. SDL и bgfx спрятаны за нашими интерфейсами — игровой код их не видит.

| Слой | Папка | Ответственность | Зависит от | Статус в v0 |
|---|---|---|---|---|
| Core | `core/` | Математика, лог, IO-стримы (`ByteBuffer`, endian), время, события, типы | — | **реализован** |
| Platform | `platform/` | Окно, ввод, ФС — обёртка над SDL3 | core | реализован (SDL3) |
| Render | `render/` | Тонкий слой над bgfx: device, текстуры/шейдеры, 2D-спрайт-батч, камера | core, platform | реализован (bgfx) |
| App | `app/` | Жизненный цикл, игровой цикл, стек сцен | всё ниже | реализован |
| Game | `game/` | Сцены состояний; в v0 — `BootScene` (тестовый рендер) | app, render | минимум |
| Заглушки | `resource/ formats/ world/ net/ ui/` | Объявленные интерфейсы под будущие подпроекты | — | **stub** |

## 4. Дерево `Client/`

```
Client/
├── CMakeLists.txt            # корневой проект клиента
├── CMakePresets.json         # win-msvc, linux-gcc, linux-clang, core-only
├── vcpkg.json                # манифест зависимостей (sdl3, bgfx)
├── .gitignore                # build/
├── README.md
├── docs/specs/               # этот документ
├── cmake/CompileShaders.cmake# интеграция bgfx shaderc (полная сборка)
├── assets/shaders/           # varying.def.sc, vs_sprite.sc, fs_sprite.sc
└── src/
    ├── CMakeLists.txt
    ├── main.cpp
    ├── core/      Types.hpp Assert.hpp Log.* io/ByteBuffer.hpp math/Math.hpp time/Clock.* event/EventBus.hpp Config.hpp
    ├── platform/  Platform.hpp Window.hpp Input.hpp FileSystem.hpp sdl/SdlPlatform.cpp
    ├── render/    RenderDevice.* Texture.hpp Shader.hpp SpriteBatch.* Camera.hpp
    ├── app/       Application.* Scene.hpp SceneStack.*
    ├── game/      BootScene.*
    ├── resource/  Vfs.hpp ResourceManager.hpp   (stub: GRF/VFS — v1)
    ├── formats/   Formats.hpp                    (stub: SPR/ACT/GND/GAT/RSW/RSM — v2)
    ├── world/     World.hpp                      (stub — v3)
    ├── net/       Net.hpp                        (stub — v4)
    └── ui/        Ui.hpp                         (stub — v5)
```

## 5. Зависимости и сборка

- **SDL3** — окно/ввод/платформа (покрывает и мобильные — в отличие от GLFW).
- **bgfx + bx + bimg** — рендер.
- Сборка: **CMake ≥ 3.24**, стандарт **C++20**. Зависимости — через **vcpkg** (manifest mode); пресеты ссылаются на `$VCPKG_ROOT`.
- Тесты v0 — минимальный собственный харнесс `tests/microtest.hpp` (ноль зависимостей; позже можно заменить на doctest).
- `core/` намеренно **без внешних зависимостей** (своя математика/IO/лог) → собирается и тестируется автономно (CMake-опция `CLIENT_CORE_ONLY=ON`), что даёт быстрый CI-таргет и offline-верификацию.

## 6. Общий роадмап (контекст границ модулей)

v0 Foundation → v1 GRF/VFS → v2 Форматы ассетов (SPR/ACT, GND/GAT/RSW, RSM) → v3 Рендер карт/моделей → v4 Сеть (расширяемые пакеты, `recvpackets-uaro.txt`) → v5 Игра/UI/Lua → v6 Мобильные бэкенды (Android/iOS) → v7 Тулинг (GRF-редактор).

## 7. Порядок реализации v0

1. Build-система: `CMakeLists.txt`, `CMakePresets.json`, `vcpkg.json`, `.gitignore`, `cmake/CompileShaders.cmake`.
2. `core/`: типы, лог, `ByteBuffer`, математика, время, события.
3. `tests/`: микро-харнесс + тесты IO/математики.
4. **Верификация:** `cmake -DCLIENT_CORE_ONLY=ON` собирает `core` + тесты, тесты зелёные (offline).
5. `platform/` (SDL3): окно, ввод, ФС.
6. `render/` (bgfx): device, текстура, шейдер, спрайт-батч, камера.
7. `app/` + `game/BootScene`: цикл + тестовый рендер.
8. Заглушки `resource/formats/world/net/ui`.
9. Шейдеры `assets/shaders/`.
10. `main.cpp`, `README.md`.
11. Финальная верификация core-таргета, коммит на `client/foundation`.

## 8. Проверяемость в текущей среде

- **Проверяется offline здесь:** конфигурация CMake core-таргета, компиляция `core/`, прогон юнит-тестов (GCC 14.2).
- **Собирается на dev-машине (не run-verified в песочнице):** `platform`/`render` и оконно-рендерный путь — нужны SDL3 + bgfx (vcpkg) и дисплей. Код пишется строго по их API.
