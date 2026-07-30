# BornRok Client

**BornRok** is a modern, cross-platform C++20 **Ragnarok Online** client for the **uAthena** server
([github.com/uaOpenKore/uAthena](https://github.com/uaOpenKore/uAthena)). Play at
**[bornrok.com](https://bornrok.com)** and **[uaro.kiev.ua](https://uaro.kiev.ua)**.

It renders the original sprite world (SPR/ACT characters, GND/GAT/RSW maps, RSM/RSM2 models) with a
modern engine, speaks the classic (eAthena/rAthena-family) network protocol, and keeps its game content
up to date through a built-in patcher.

**Download the binary builds:** <https://uaro.kiev.ua/forum/showthread.php?tid=212>

Rendering is abstracted through **bgfx** (D3D11/12, OpenGL, GLES, Metal, Vulkan). Windowing, input and
audio use **SDL3** on desktop/mobile; the Xbox/UWP build swaps in a WinRT backend (SDL3 has no WinRT
port) and drives XAudio2. Game code never sees SDL or bgfx directly — everything is behind the
`platform/` and `render/` interfaces, so a backend can be swapped per platform by changing one CMake
target. The original Windows binary in `winEXE/` is used only as a behavioural/RE reference.

> Internal namespace and some asset roots keep the historical `uaro` name; the product is **BornRok**.

## Supported

**Input / gameplay:** keyboard + mouse · touchscreens · gamepads.

**Platforms (tested):** Windows · Linux · Android · macOS.

**Architectures:** Intel/AMD x64 · ARM64.

**Supported but not yet tested:** iOS · Xbox One and newer · Steam Deck · Nintendo Switch and newer ·
Sony PlayStation 4/5 (vendor API/SDK support not yet implemented).

**Graphics enhancements:** FSR1 upscaling · SSAA · normal maps · high-quality (HD) textures & sprites.
Plus day/night and dungeon lighting, volumetric light / god rays, and HDR output.

`Client/VERSION` (`0.0.N.0`) is the single source of truth for the client version (login "AlphaN"
derives from it, and it feeds the MSIX package version).

## Layout

```
src/core/      dependency-free foundation: math, io (ByteBuffer), log, time, events, ini, Lang (i18n)
src/platform/  window / input / filesystem / console services (SDL3 desktop+mobile; WinRT for Xbox)
src/render/    bgfx device, textures, shader loader, 2D sprite batch, camera, HDR/FSR
src/formats/   asset parsers: SPR/ACT, GND/GAT/RSW, RSM/RSM2, STR, IMF/FLC, GR2, PNG-sprite, BMP/TGA
src/resource/  VFS (GRF v0x1xx/0x200/0x300 + ZIP64), item/skill DBs, palettes, GRF data tables
src/world/     map/ground/model/water meshes, pathfinding (GAT A*), lighting
src/net/       socket, packet protocol (extensible packet table), session
src/audio/     SFX + BGM mixer (dr_wav/dr_mp3/dr_flac; SDL3 core audio, or XAudio2 on Xbox)
src/ui/        embedded bitmap font, immediate-mode widgets, original login/skin loader
src/patcher/   downloads.list patcher, content-quality resolution, platform detection
src/app/       application/loop, scene stack, config + i18n
src/game/      the game itself: login, char-select/-create, map scene, combat, skills, all UI windows
assets/        bgfx shader sources (.sc), embedded UI textures, texts/<lang>.cfg translations
packaging/     flatpak + steam packaging; win/ + uwp/ + android/ + ios/ + macos/ hold per-OS shells
tests/         core/format unit tests (in-tree microtest harness)
docs/          build guides, format notes, protocol audits, effect/RE documentation
```

Library layering (each depends only on the ones below; SDL/bgfx hidden behind interfaces):

`core` → `platform` → `render` → `resource`/`formats`/`world`/`net`/`audio`/`ui`/`patcher` → `app` → `game`

## Building

The full client needs **CMake 3.24+**, a **C++20** compiler, and **vcpkg** (deps `sdl3`, `bgfx` are
declared in `vcpkg.json`; per-platform triplets in `vcpkg-triplets/`). Shaders are compiled to bytecode
next to the executable by `cmake/CompileShaders.cmake` (bgfx `shaderc`).

### One-command scripts

```sh
export VCPKG_ROOT=/path/to/vcpkg
./scripts/build-linux.sh              # Linux x64 + ARM64 (pass --no-arm for x64 only)
./scripts/build-macos-universal.sh    # macOS universal (x64 + arm64)
./scripts/build-flatpak.sh [arm64]    # Flatpak bundle (x86_64 default, or aarch64)
./scripts/build-steam.sh              # Steam depot layout
pwsh ./scripts/build-windows.ps1      # Windows (x64) + MSIX
```

### CMake presets (manual)

```sh
cmake --preset win-msvc          # or any preset from the table below
cmake --build --preset win-msvc
```

| Preset | Target | Backend |
|---|---|---|
| `win-msvc` | Windows x64 | SDL3 + D3D11/12 |
| `win-arm64` | Windows on ARM (cross from x64) | SDL3 |
| `linux-gcc-release` / `linux-clang-release` | Linux x64 (+ AVX2 sibling exe) | SDL3 + GL/Vulkan |
| `linux-arm-gcc-release` | Linux ARM64 (cross from x64) | SDL3 |
| `mac-clang-release` / `mac-arm64-release` | macOS Intel / Apple Silicon | SDL3 + Metal |
| `ios` | iOS / iPadOS | SDL3 + Metal |
| `xbox-uwp` | Xbox (UWP / Dev Mode) | WinRT + D3D12 + XAudio2 |
| `nx` / `prospero` / `orbis` | Switch / PS5 / PS4 (SDK-free scaffold) | vendor |
| `core-only` | `core/` + unit tests only | — |

Android builds from `android/` (Gradle → `libmain.so`). macOS universal (x64+arm64) via
`scripts/build-macos-universal.sh`.

### Core only — no external dependencies (CI / quick checks)

```sh
cmake --preset core-only
cmake --build build/core
ctest --test-dir build/core --output-on-failure
```

Platform-specific setup lives in `docs/`: [Linux](docs/BUILD-linux.md),
[Windows + VS Code](docs/BUILD-windows-vscode.md), [Android](docs/android-build-km.md),
[iOS](docs/ios-build-km.md), [macOS](docs/macos-build-km.md), [Xbox/UWP](docs/xbox-uwp-build-km.md),
[Windows MSIX](docs/windows-msix-km.md).

## Content & the patcher

The client ships with a built-in patcher: at launch it fetches `downloads.list`, verifies (SHA512) and
downloads any missing/updated packs into a writable `content/` folder, then mounts them. Packs are
priority-ordered ZIP/GRF archives (root data, per-quality texture/sprite tiers, event packs).

On **baked** distributions the content is shipped inside the package and the patcher is skipped
entirely — closed consoles, iOS, Steam Deck, and packaged desktop installs (MSIX / Flatpak). A loose
desktop exe and Android still patch. Texture and sprite quality default to **1k** for everyone; 2k/4k
are an opt-in in the in-game settings (hidden where quality packs aren't fetched).

## Command-line flags

```
BornRok                 normal launch (patcher -> login), unless the platform is baked
BornRok --no-patch      skip the patcher, go straight to login (content-maker GRF testing)
BornRok --test          offline: auto-load prontera if data is present, else the sprite-test scene
BornRok --view          content browser (2D sprite vs 3D model pairing tool)
BornRok <mapname>       offline map viewer
```

## Features

Full classic gameplay: login / character select / creation, map rendering with day/night + dungeon
lighting, mobs/NPCs/warps, movement with client-side prediction, melee/ranged/skill combat with damage
numbers and hit feedback, skills with status icons and `.str`/coded effects, inventory / equipment /
skills / stats windows, party / guild / friends / chat rooms, trade / vending / Kafra storage, pets,
homunculus & mercenary, quest journal, minimap, emotes. Rendering extras: HD textures, PNG sprites,
normal maps, volumetric light / god rays, HDR output with optional FSR1 upscale, RSM2 props, water.
Input: keyboard/mouse, full gamepad scheme, and a touchscreen mode.

## License

Proprietary (`LicenseRef-proprietary`). The `winEXE/` reference binary and any third-party code under
`third_party/` retain their own licenses.
