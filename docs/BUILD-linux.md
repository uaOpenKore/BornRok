# Сборка клиента на Linux (Debian/Ubuntu)

**Пресеты (по умолчанию — Release):**
- `linux-gcc` / `linux-clang` — **дефолт: Release + статичный SDL3** (самодостаточный
  бинарь, см. раздел 5). Первый configure долго собирает SDL3 через vcpkg.
- `linux-gcc-debug` / `linux-clang-debug` — **Debug + системный SDL3** (быстрая
  итерация; нужен `libsdl3-dev`). Разделы 2–4 ниже — про этот путь.
- `linux-gcc-release` / `linux-clang-release` — то же, что дефолтные (алиасы).

Голый `cmake -B build` без пресета тоже даёт **Release** (дефолт в CMakeLists).

На Linux **SDL3 берётся из системы** (apt) в debug-пресетах, а release-пресеты
собирают SDL3 статикой из vcpkg. Через vcpkg собираются только bgfx и
zlib. Это специально: vcpkg-сборка SDL3 из исходников тянет тяжёлое дерево
(libxcrypt/libsystemd/...), которое на минимальной системе мучительно собирать.
`vcpkg.json` объявляет `sdl3` только для не-Linux платформ.

## 1. Быстрая проверка тулчейна — `core/` (без зависимостей)

```bash
sudo apt update
sudo apt install -y build-essential cmake git
cd uAthena/Client
cmake --preset core-only
cmake --build build/core
ctest --test-dir build/core --output-on-failure   # ожидаем 100% passed
```

## 2. Зависимости для полного клиента

```bash
sudo apt install -y build-essential cmake git ninja-build pkg-config curl zip unzip tar \
  linux-libc-dev \
  libsdl3-dev libzstd-dev \
  libgl1-mesa-dev libegl1-mesa-dev \
  libx11-dev libxft-dev libxext-dev libxrandr-dev libxi-dev libxcursor-dev libxinerama-dev \
  libxfixes-dev libxrender-dev libxss-dev libxkbcommon-dev libxtst-dev
```
- **`libsdl3-dev`** — системный SDL3 (на Debian 13 / свежей Ubuntu есть). Проверить:
  `apt-cache policy libsdl3-dev`. Если пакета нет — см. «Если нет libsdl3-dev» ниже.
- GL/X11 dev-библиотеки нужны для сборки bgfx.

## 3. vcpkg (только для bgfx + zlib)

```bash
git clone https://github.com/microsoft/vcpkg ~/vcpkg
~/vcpkg/bootstrap-vcpkg.sh
export VCPKG_ROOT=~/vcpkg
echo 'export VCPKG_ROOT=~/vcpkg' >> ~/.bashrc
```

## 4. Сборка и запуск

```bash
cd uAthena/Client
rm -rf build/linux-gcc
cmake --preset linux-gcc        # vcpkg соберёт только bgfx+zlib (быстрее), SDL3 — системный
cmake --build build/linux-gcc
./build/linux-gcc/src/uaro_client
```

## 5. Release-сборка (полностью статичная)

Пресеты из разделов выше собирают `Debug` и линкуют **системный SDL3** (`.so`).
**Release-пресеты собирают полностью статичный бинарь** (S.: «все библиотеки
статично»): SDL3 берётся из vcpkg как статик-либа (а не системный `.so`), всё
остальное (bgfx/zlib/zstd/curl/vorbis/harfbuzz) на Linux vcpkg и так собирает
статикой, libstdc++/libgcc вшиты (`-static-libstdc++ -static-libgcc`). Динамик
остаётся только у glibc / GL / X11 — по-настоящему статичный GUI-бинарь на glibc
невозможен (GL-драйвер и NSS грузятся через `dlopen`).

Статичный SDL3 включается автоматически: release-пресеты активируют vcpkg-фичу
`linux-static` (`VCPKG_MANIFEST_FEATURES=linux-static`), и vcpkg собирает SDL3 из
исходников **только с x11** (`default-features:false` + `x11`). dbus/ibus/wayland
намеренно выключены — иначе SDL3 тянет `libsystemd`, а тот требует meson + python
venv (кроличья нора зависимостей). Для игры на X11/XWayland x11-бэкенда достаточно,
доп. apt-депов сверх раздела 2 не нужно.

`libsdl3-dev` (apt) для release НЕ нужен — SDL3 идёт из vcpkg. Он нужен только
для быстрых debug-пресетов (`linux-*-debug`).

```bash
cd uAthena/Client
cmake --preset linux-gcc-release           # первый раз vcpkg соберёт SDL3 (долго)
cmake --build build/linux-gcc-release -j    # -j = все ядра
ldd build/linux-gcc-release/src/uaro_client # проверка: только libc/GL/X11/glibc-семья
./build/linux-gcc-release/src/uaro_client
```

Clang — аналогично пресетом `linux-clang-release` (каталог
`build/linux-clang-release`).

**Без пресетов** (если нужно задать свой каталог/флаги вручную):

```bash
cmake -B build/linux-release -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake \
  -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
cmake --build build/linux-release -j
```

Нюансы:
- Генератор одноконфигурационный (Unix Makefiles), поэтому тип сборки
  фиксируется на `configure` — флаг `--config Release` у `cmake --build` НЕ
  нужен (он только для multi-config, напр. MSVC).
- Не собирать юнит-тесты в релизе — добавь `-DCLIENT_BUILD_TESTS=OFF`.
- Уменьшить бинарь — `strip build/linux-gcc-release/src/uaro_client`.
- Ассеты (GRF/`data`) и шейдеры кладутся рядом с бинарём так же, как в дебаге.

## 6. Кросс-сборка под Linux ARM (aarch64) с x86_64-хоста

Собираем **arm64-бинарь на обычном x64-Linux** (у нас нет ARM-железа). Аналог
win-arm64: aarch64 GNU cross-toolchain + отдельный vcpkg-триплет, host-shaderc
переиспользуется из x64-сборки (arm64-shaderc не запускается на x64-хосте).

**Предусловия (Debian/Ubuntu):**
```bash
# 1. aarch64 cross-компилятор
sudo apt install -y crossbuild-essential-arm64   # даёт aarch64-linux-gnu-gcc/g++

# 2. arm64 multiarch dev-библиотеки (для SDL3/bgfx под целевую арх)
sudo dpkg --add-architecture arm64
sudo apt update
sudo apt install -y \
  libgl1-mesa-dev:arm64 libegl1-mesa-dev:arm64 \
  libwayland-dev:arm64 libwayland-egl1:arm64 libwayland-egl-backend-dev:arm64 \
  libx11-dev:arm64 libxft-dev:arm64 libxext-dev:arm64 libxrandr-dev:arm64 \
  libxi-dev:arm64 libxcursor-dev:arm64 libxinerama-dev:arm64 libxfixes-dev:arm64 \
  libxrender-dev:arm64 libxss-dev:arm64 libxkbcommon-dev:arm64 libxtst-dev:arm64
```
> Хотя SDL3 у нас x11-only, mesa `libEGL` при линковке всё равно ссылается на
> `libwayland-egl` -> arm64 wayland dev-либы обязательны, иначе линкер падает с
> `cannot find -lwayland-egl`.

> В **Debian** (в отличие от Ubuntu) arm64 отдаётся из того же `deb.debian.org` —
> отдельный ports-репозиторий добавлять НЕ нужно, хватает `dpkg --add-architecture arm64`
> + `apt update`. Проверь, что в `/etc/apt/sources.list(.d)` в строках нет ограничения
> `Architectures: amd64` (оно бы резало arm64).

### Грабли: `libelf1t64:arm64 не будет установлен` (t64 / backports рассинхрон)

Самый частый затык. `apt install ...:arm64` падает с
`mesa-libgallium:arm64 : Depends: libelf1t64:arm64 (>= 0.142) but it is not going to be installed`.

Причина: **t64-пакеты помечены `Multi-Arch: same`** — версия amd64 и arm64 обязана
совпадать **1:1**. Если твой установленный amd64-пакет пришёл из `trixie-backports`
(или proposed), а arm64 по умолчанию берёт версию из `main` (у backports приоритет
100, он не выбирается автоматически) — версии расходятся и apt отказывается.

Диагностика — сравни версии на обеих арках:
```bash
apt policy libelf1t64 libelf1t64:arm64
# Installed (amd64): 0.195-1~bpo13+1  (trixie-backports)
# Candidate (arm64): 0.192-4          (trixie/main)  <-- не совпадает!
```
Лечение — поставить arm64 ТОЙ ЖЕ версией, что стоит amd64 (подставь свою из вывода):
```bash
sudo apt install -y libelf1t64:arm64=0.195-1~bpo13+1
```
После этого повторить установку arm64 dev-либ. Если тем же способом ругнётся другая
`Multi-Arch: same` либа из backports — та же схема: `apt policy <пакет>` → доставить
`:arm64=<та же версия>`. (libelf обычно единственный такой случай.)

> Если apt-каша не разгребается — альтернатива без multiarch: собрать нативно внутри
> arm64-контейнера под QEMU (`qemu-user-static` + `docker run --platform linux/arm64`,
> обычный `linux-gcc-release` в отдельный `build/`-каталог), либо отдельный arm64-sysroot
> и указать его в `cmake/linux-arm64-cross.cmake` через `-DCLIENT_ARM64_TOOL_PREFIX` /
> `CMAKE_FIND_ROOT_PATH`.

**Сборка:**
```bash
cd uAthena/Client
# 1) СНАЧАЛА собрать x64-релиз -- он даёт готовые шейдеры (build/linux-gcc-release/src/shaders)
cmake --preset linux-gcc-release && cmake --build build/linux-gcc-release -j

# 2) теперь arm64-кросс (vcpkg долго собирает deps под arm64 из исходников)
cmake --preset linux-arm-gcc-release
cmake --build build/linux-arm -j
file build/linux-arm/src/uaro_client   # -> ELF 64-bit LSB, ARM aarch64
```
Готовый бинарь: `Client/build/linux-arm/src/uaro_client`. На ARM **нет AVX2** —
второй `uaro_client-avx2` под arm64 не собирается (только базовый).

**Шейдеры не компилируются под arm** — пресет берёт готовый байткод из x64-сборки
(`CLIENT_PREBUILT_SHADERS=build/linux-gcc-release/src/shaders`). Скомпилированные
шейдеры — это GPU-байткод по профилю рендера (glsl/spirv), он НЕ зависит от CPU-арх,
поэтому arm переиспользует `.bin` из x64 без shaderc/glslang. Поэтому **шаг 1
обязателен ПЕРЕД конфигурацией шага 2** (наличие `.bin` проверяется на этапе
configure). Если хочешь взять шейдеры из другого места (например из Windows-сборки) —
`-DCLIENT_PREBUILT_SHADERS=<путь к shaders/ с подпапками glsl/spirv/...>`.

Свой префикс тулчейна — `-DCLIENT_ARM64_TOOL_PREFIX=<prefix>` (по умолчанию
`aarch64-linux-gnu`).

> ⚠️ Пока **не протестировано на живом ARM-железе** — кросс-сборочная обвязка
> готова; рантайм-баги правим по логам с реального устройства.

## Если нет `libsdl3-dev` (старый apt)

Тогда либо собрать SDL3 из исходников вручную (`git clone SDL`, `cmake`, `make install`),
либо вернуть `sdl3` в vcpkg (убрать `"platform": "!linux"` в `vcpkg.json`) и доставить
сборочные зависимости systemd: `meson python3-jinja2 python3-pyelftools gperf libcap-dev
libmount-dev`.

## Нюансы

- **Wayland:** проверенный путь — X11 / XWayland. Нативный Wayland у bgfx требует доп.
  настройки.
- **Шейдеры:** если `shaderc` (из bgfx) не найден — окно откроется, но без спрайтов
  (только фон), в лог пойдёт предупреждение.

## Пакеты для дистрибуции (Flatpak + SteamOS/Steam)

Готовые скрипты собирают дистрибутивные пакеты из **предсобранного** статического
linux-x86_64 бинарника (каждый скрипт сам вызывает `build-linux.sh --no-arm`).

```bash
# всё сразу (Flatpak + Steam):
VCPKG_ROOT=~/vcpkg ./scripts/build-packages.sh
# по отдельности:
VCPKG_ROOT=~/vcpkg ./scripts/build-packages.sh flatpak
VCPKG_ROOT=~/vcpkg STEAM_APPID=<appid> STEAM_DEPOTID=<depotid> ./scripts/build-packages.sh steam
```

**Flatpak** (`scripts/build-flatpak.sh`, манифест `packaging/flatpak/`): нужен
`flatpak-builder` + runtime/SDK Freedesktop 24.08. Результат — `build/BornRok.flatpak`
(`flatpak install --user build/BornRok.flatpak`, запуск `flatpak run com.bornrok.BornRok`).
На Steam Deck / SteamOS ставится сайдлоадом так же.

**SteamOS / Steam** (`scripts/build-steam.sh`, шаблоны `packaging/steam/*.vdf.in`):
Steam Deck запускает игру нативно через Steam Linux Runtime — статический x86_64 бинарник
подходит как есть. Скрипт делает:
- `build/steam/content/` — полезная нагрузка депо (бинарник + `run.sh`; данные тянет
  встроенный патчер при первом запуске, либо задай `STEAM_DATA_DIR=/путь` чтобы вшить их в депо);
- `build/steam/app_build.vdf` + `depot_build.vdf` — скрипты SteamPipe с подставленными
  `STEAM_APPID`/`STEAM_DEPOTID` и абсолютными путями;
- `build/BornRok-steamos-x86_64.tar.gz` — портативный tar (без Steamworks-аккаунта:
  распаковать на Деке, запустить `./run.sh`, или добавить как non-Steam игру).

Загрузка в Steam:
```bash
steamcmd +login <user> +run_app_build "$PWD/build/steam/app_build.vdf" +quit
```
`STEAM_APPID`/`STEAM_DEPOTID` берутся из твоего приложения в Steamworks; без них в
`app_build.vdf` останутся плейсхолдеры.
