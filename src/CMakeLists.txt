# ---------------------------------------------------------------------------
# Console backend switch (console-port prep). Empty = desktop/SDL (default): NOTHING
# changes. When a console toolchain sets it (prospero/nx/xbox), we define CLIENT_CONSOLE
# so the engine routes user data (configs/saves) through the ConsoleServices container
# API -- the console's dedicated storage -- instead of loose files. Must be set BEFORE the
# targets below so add_compile_definitions applies to them. See docs/console-port-plan-km.md.
# ---------------------------------------------------------------------------
set(CLIENT_CONSOLE_BACKEND "" CACHE STRING "Console backend (prospero/nx/xbox); empty = desktop. Defines CLIENT_CONSOLE.")
if(CLIENT_CONSOLE_BACKEND)
  add_compile_definitions(CLIENT_CONSOLE=1 CLIENT_CONSOLE_BACKEND_NAME="${CLIENT_CONSOLE_BACKEND}")
  message(STATUS "Console backend: ${CLIENT_CONSOLE_BACKEND} (CLIENT_CONSOLE defined -- user data via ConsoleServices)")
endif()

# ---------------------------------------------------------------------------
# core: dependency-free foundation (math, io, log, time, events).
# Always built; safe to compile with nothing but a C++20 compiler.
# ---------------------------------------------------------------------------
add_library(client_core STATIC
  core/Log.cpp
  core/compress/Lz4.cpp
  core/time/Clock.cpp
  core/thread/ThreadRole.cpp
  core/crypto/Sha512.cpp
)
target_include_directories(client_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(client_core PUBLIC cxx_std_20)
find_package(Threads REQUIRED)
target_link_libraries(client_core PUBLIC Threads::Threads)
add_library(uaro::core ALIAS client_core)

# ---------------------------------------------------------------------------
# resource: GRF archive reader + virtual filesystem. Depends only on core + zlib
# (no SDL/bgfx), so it is built and unit-tested in the offline/core-only config
# too, whenever zlib is available.
# ---------------------------------------------------------------------------
find_package(ZLIB GLOBAL)  # GLOBAL so tests/ (a sibling scope) sees ZLIB::ZLIB
if(NOT ZLIB_FOUND AND CLIENT_CONSOLE_BACKEND)
  # Console cross-build: vcpkg can't build zlib for the vendor triplet, but the vendor SDK ships
  # zlib.h (already on the toolchain's include path). Provide a HEADER-ONLY ZLIB::ZLIB so the
  # resource layer COMPILES now; the real libz is resolved at the final-title LINK -- the same
  # deferred stage as the vendor GPU backend (NVN/AGC/GNM). A desktop build never reaches here
  # (ZLIB_FOUND via vcpkg). Provide a real libz for the shipping title.
  find_path(UARO_CONSOLE_ZLIB_H zlib.h)
  if(UARO_CONSOLE_ZLIB_H)
    add_library(ZLIB::ZLIB INTERFACE IMPORTED GLOBAL)
    set_target_properties(ZLIB::ZLIB PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${UARO_CONSOLE_ZLIB_H}")
    set(ZLIB_FOUND TRUE)
    message(STATUS "zlib: vendor SDK header at ${UARO_CONSOLE_ZLIB_H} -> ZLIB::ZLIB header-only "
                   "(compiles now; link a real libz at the title stage)")
  endif()
endif()
if(ZLIB_FOUND)
  add_library(client_resource STATIC
    resource/Grf.cpp
    resource/GrfDecrypt.cpp
    resource/Zip.cpp
    resource/Vfs.cpp
    resource/ContentRoute.cpp
    resource/ItemDb.cpp
    resource/EmbeddedQuestTitles.cpp
    resource/LuaTables.cpp
    resource/GrfData.cpp
    resource/CardIllust.cpp
  )
  target_include_directories(client_resource PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(client_resource PUBLIC uaro::core PRIVATE ZLIB::ZLIB uaro::lua51)
  add_library(uaro::resource ALIAS client_resource)
else()
  message(WARNING "ZLIB not found: resource/ (GRF/VFS) will not be built")
endif()

# ---------------------------------------------------------------------------
# formats: asset-format parsers (SPR now; ACT/GND/GAT/RSW/RSM later). core only,
# so it builds and is unit-tested in the offline/core-only config.
# ---------------------------------------------------------------------------
add_library(client_formats STATIC
  formats/Spr.cpp
  formats/Act.cpp
  formats/PngSprite.cpp
  formats/Str.cpp
  formats/Evff.cpp
  formats/Gat.cpp
  formats/Gnd.cpp
  formats/Rsw.cpp
  formats/Rsm.cpp
  formats/Bmp.cpp
  formats/Tga.cpp
  formats/Imf.cpp
  formats/Flc.cpp
  formats/Png.cpp
  formats/Webp.cpp
  formats/ImageIO.cpp
  formats/Gr2.cpp
  formats/Gr2Oodle.cpp
  formats/UnityFs.cpp
  formats/UnitySerialized.cpp
  formats/UnityTexture.cpp
  formats/Fsb5.cpp
  formats/UnityMesh.cpp
  formats/UnitySkeleton.cpp
  formats/UnityAvatar.cpp
  formats/UnityAnimClip.cpp
  formats/UnityParticle.cpp
)
target_include_directories(client_formats PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(client_formats PUBLIC uaro::core)
# zstd: the RoM UnityFS bundles compress data blocks with Zstandard (method 5). Optional —
# without it the UnityFS reader still parses containers but can't inflate zstd blocks.
find_package(zstd CONFIG QUIET)
set(UARO_ZSTD_TARGET "")
if(TARGET zstd::libzstd)          # vcpkg >= 1.5.x exports this umbrella target
  set(UARO_ZSTD_TARGET zstd::libzstd)
elseif(TARGET zstd::libzstd_static)
  set(UARO_ZSTD_TARGET zstd::libzstd_static)
elseif(TARGET zstd::libzstd_shared)
  set(UARO_ZSTD_TARGET zstd::libzstd_shared)
endif()
if(UARO_ZSTD_TARGET)
  target_compile_definitions(client_formats PRIVATE CLIENT_WITH_ZSTD=1)
  target_link_libraries(client_formats PRIVATE ${UARO_ZSTD_TARGET})
  message(STATUS "zstd: ${UARO_ZSTD_TARGET} -> UnityFS zstd blocks ENABLED")
else()
  find_library(UARO_ZSTD_LIB zstd)
  find_path(UARO_ZSTD_INC zstd.h)
  if(UARO_ZSTD_LIB AND UARO_ZSTD_INC)
    target_compile_definitions(client_formats PRIVATE CLIENT_WITH_ZSTD=1)
    target_include_directories(client_formats PRIVATE ${UARO_ZSTD_INC})
    target_link_libraries(client_formats PRIVATE ${UARO_ZSTD_LIB})
    message(STATUS "zstd: system lib -> UnityFS zstd blocks ENABLED")
  else()
    message(WARNING "zstd NOT FOUND: RoM (.unity3d) content will not load. "
                    "Re-run the CMake configure so vcpkg installs zstd from vcpkg.json.")
  endif()
endif()
# libwebp: optional HD image format (.webp — smaller than PNG, alpha supported). Without it
# decodeImage simply won't handle WebP (Webp::decode returns nullopt); other formats unaffected.
# On the Windows/console presets the triplet is *-windows-static, so vcpkg builds libwebp as a static
# archive and this find_package pulls the static WebP::* targets -> libwebp is baked into the exe (no
# DLL). NOT QUIET so the configure log clearly shows found/not-found (S.: "добавь libwebp в статичную
# линковку" -- confirm the static target actually resolves).
find_package(WebP CONFIG)
if(NOT WebP_FOUND)
  find_package(WebP)          # module mode (system FindWebP, if any)
endif()
message(STATUS "webp: WebP_FOUND=${WebP_FOUND} WebP_DIR=${WebP_DIR}")
set(UARO_WEBP_TARGET "")
foreach(_wt WebP::webpdecoder WebP::webp WebP::libwebp WebP::libwebpdecoder webp webpdecoder)
  if(NOT UARO_WEBP_TARGET AND TARGET ${_wt})
    set(UARO_WEBP_TARGET ${_wt})
  endif()
endforeach()
# Fallback 1: no CMake package target -> find the lib+header by hand (also covers the vcpkg static
# tree directly) so webp still enables instead of silently ignoring every .webp override.
if(NOT UARO_WEBP_TARGET)
  find_library(UARO_WEBP_LIB NAMES webp libwebp webpdecoder libwebpdecoder)
  find_path(UARO_WEBP_INC NAMES webp/decode.h)
  if(UARO_WEBP_LIB AND UARO_WEBP_INC)
    set(UARO_WEBP_TARGET "${UARO_WEBP_LIB}")
    set(UARO_WEBP_MANUAL_INC "${UARO_WEBP_INC}")
    # static libwebp needs sharpyuv too; link it if it sits alongside.
    find_library(UARO_SHARPYUV_LIB NAMES sharpyuv libsharpyuv)
    if(UARO_SHARPYUV_LIB)
      list(APPEND UARO_WEBP_TARGET "${UARO_SHARPYUV_LIB}")
    endif()
  endif()
endif()
# Fallback 2: pkg-config (system libwebp on Linux/macOS).
if(NOT UARO_WEBP_TARGET)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(UARO_WEBP_PC QUIET libwebp)
    if(UARO_WEBP_PC_FOUND)
      set(UARO_WEBP_TARGET "${UARO_WEBP_PC_LINK_LIBRARIES}")
      set(UARO_WEBP_MANUAL_INC "${UARO_WEBP_PC_INCLUDE_DIRS}")
    endif()
  endif()
endif()
if(UARO_WEBP_TARGET)
  # The decode preference lives in readPreferPng (client_resource), so BOTH libs need the flag: the
  # resource lib to gate the .webp preference, the formats lib to compile the actual decoder.
  target_compile_definitions(client_formats PRIVATE CLIENT_WITH_WEBP=1)
  target_compile_definitions(client_resource PRIVATE CLIENT_WITH_WEBP=1)
  target_link_libraries(client_formats PRIVATE ${UARO_WEBP_TARGET})
  if(UARO_WEBP_MANUAL_INC)
    target_include_directories(client_formats PRIVATE ${UARO_WEBP_MANUAL_INC})
  endif()
  message(STATUS "webp: ${UARO_WEBP_TARGET} -> .webp images ENABLED")
else()
  # No libwebp: readPreferPng must NOT prefer a .webp it cannot decode (that rendered every override
  # as a white/undecodable texture). Without the flag it falls back to .png/.bmp -> correct render.
  message(STATUS "webp: libwebp not found -> .webp preference DISABLED (PNG/BMP/TGA still work)")
endif()

add_library(uaro::formats ALIAS client_formats)

# ---------------------------------------------------------------------------
# world: builds renderable geometry from parsed assets (GND -> ground mesh).
# core + formats only (no SDL/bgfx), so it builds and is tested offline.
# ---------------------------------------------------------------------------
add_library(client_world STATIC
  world/GroundMesh.cpp
  world/ModelMesh.cpp
  world/SpriteComposer.cpp
  world/Pathfind.cpp
  world/RomModel.cpp
)
target_include_directories(client_world PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(client_world PUBLIC uaro::core uaro::formats)
add_library(uaro::world ALIAS client_world)

# MapLoader pulls assets through the VFS (resource layer), so it is compiled only
# when zlib/resource are available.
if(TARGET uaro::resource)
  target_sources(client_world PRIVATE world/MapLoader.cpp)
  target_link_libraries(client_world PUBLIC uaro::resource)
endif()

# ---------------------------------------------------------------------------
# net: cross-platform TCP sockets + the login/char/map packet codec + clientinfo
# parsing. core only (plus the OS socket lib), so the codec is unit-tested offline.
# ---------------------------------------------------------------------------
add_library(client_net STATIC
  net/Socket.cpp
  net/Connection.cpp
  net/Protocol.cpp
  net/PacketTable.cpp
  net/ClientInfo.cpp
  net/JobNames.cpp
  net/SkillNames.cpp
  net/SkillCastDelay.cpp
)
target_include_directories(client_net PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(client_net PUBLIC uaro::core)
if(WIN32)
  target_link_libraries(client_net PUBLIC ws2_32)
endif()
add_library(uaro::net ALIAS client_net)

# ---------------------------------------------------------------------------
# patcher: pre-game updater core (manifest parse + platform id). Depends on core
# only, so it builds and is unit-tested in the offline config. The HTTP layer
# (libcurl) + the in-client UI phase are added in the full build below.
# ---------------------------------------------------------------------------
add_library(client_patcher STATIC
  patcher/PatchManifest.cpp
  patcher/PlatformId.cpp
  patcher/ContentQuality.cpp
  patcher/PatcherConfig.cpp
  patcher/GDriveUrl.cpp
)
target_include_directories(client_patcher PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(client_patcher PUBLIC uaro::core)
add_library(uaro::patcher ALIAS client_patcher)

if(CLIENT_CORE_ONLY)
  message(STATUS "uaRO client: core-only build, skipping platform/render/app")
  return()
endif()

if(NOT TARGET uaro::resource)
  message(FATAL_ERROR "ZLIB is required for the full client build (resource layer)")
endif()

# ---------------------------------------------------------------------------
# Full build: pull in windowing + rendering backends.
# ---------------------------------------------------------------------------
# Xbox (UWP / Dev Mode): the native WinRT backend (platform/winrt/) replaces the SDL platform layer,
# so SDL is not needed and audio is stubbed for the first bring-up. Selected by
# CLIENT_CONSOLE_BACKEND=xbox (which also defines CLIENT_CONSOLE). Needs a WindowsStore toolchain
# (CMAKE_SYSTEM_NAME=WindowsStore) + C++/WinRT. See the "xbox-uwp" preset + docs/xbox-uwp-build-km.md.
set(CLIENT_XBOX OFF)
if(CLIENT_CONSOLE_BACKEND STREQUAL "xbox")
  set(CLIENT_XBOX ON)
  add_compile_definitions(CLIENT_UWP=1)  # RenderDevice -> Direct3D12; applies to targets defined below
  # C++/WinRT (winrt/base.h) picks <coroutine> vs the deprecated <experimental/coroutine> by the C++
  # level (__cpp_lib_coroutine / _MSVC_LANG / __cplusplus). The UWP project defaults can drop the
  # standard back below C++20, and MSVC also leaves __cplusplus at 199711 without /Zc:__cplusplus, so
  # cppwinrt pulled <experimental/coroutine> (which errors without /await). Force C++20 + real
  # __cplusplus on every xbox TU so it selects the standard <coroutine>.
  add_compile_options(/std:c++20 /Zc:__cplusplus)
  message(STATUS "uaRO: Xbox UWP target (WinRT platform, SDL disabled, audio stubbed)")
endif()

# Native consoles (Nintendo Switch / PlayStation): like Xbox they SWAP the platform TU (console/<vendor>/
# instead of platform/sdl/) and drop SDL, but they keep an ordinary main()/nnMain() entry (no UWP/WinRT).
# Selected by CLIENT_CONSOLE_BACKEND=nx|prospero|orbis. The vendor SDK toolchain is supplied by the
# licensee (see the "nx"/"prospero" presets + docs/switch-build-km.md, docs/ps-build-km.md). The
# console/<vendor>/*.cpp files carry `// TODO(SDK):` markers the licensee fills before building.
set(CLIENT_NX OFF)
set(CLIENT_PROSPERO OFF)   # PS5
set(CLIENT_ORBIS OFF)      # PS4
if(CLIENT_CONSOLE_BACKEND STREQUAL "nx")
  set(CLIENT_NX ON)
  add_compile_definitions(CLIENT_NX=1)
  message(STATUS "uaRO: Nintendo Switch target (console/nx platform, NVN renderer, SDL disabled)")
elseif(CLIENT_CONSOLE_BACKEND STREQUAL "prospero")
  set(CLIENT_PROSPERO ON)
  add_compile_definitions(CLIENT_PROSPERO=1)
  message(STATUS "uaRO: PlayStation 5 target (console/prospero platform, AGC renderer, SDL disabled)")
elseif(CLIENT_CONSOLE_BACKEND STREQUAL "orbis")
  set(CLIENT_ORBIS ON)
  add_compile_definitions(CLIENT_ORBIS=1)
  message(STATUS "uaRO: PlayStation 4 target (console/prospero platform, GNM renderer, SDL disabled)")
endif()
# True for any backend that provides its own Platform/Window TU and must not link SDL.
if(CLIENT_NX OR CLIENT_PROSPERO OR CLIENT_ORBIS)
  set(CLIENT_CONSOLE_NATIVE ON)
else()
  set(CLIENT_CONSOLE_NATIVE OFF)
endif()

if(NOT CLIENT_XBOX AND NOT CLIENT_CONSOLE_NATIVE)
  find_package(SDL3 CONFIG REQUIRED)
endif()
find_package(bgfx CONFIG REQUIRED)

# Patcher HTTP layer (libcurl). Added to the patcher lib only in the full build,
# where curl is available; the core-only test build skips it.
find_package(CURL CONFIG REQUIRED)
target_sources(client_patcher PRIVATE net/Http.cpp patcher/PatchRunner.cpp)
target_link_libraries(client_patcher PUBLIC CURL::libcurl)

# platform: the concrete Platform::*/Window::* the engine links (compile-time swap, no vtable).
# Desktop/Android = SDL3; Xbox = native WinRT (platform/winrt/) + no SDL link.
if(CLIENT_XBOX)
  add_library(client_platform STATIC
    platform/winrt/WinrtPlatform.cpp
    platform/winrt/WinrtConsoleServices.cpp
    platform/ConsoleServices.cpp   # desktop file-backed impl kept as the default before install
  )
  target_include_directories(client_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  # windowsapp = the C++/WinRT umbrella import lib (CoreWindow, Gaming.Input, Storage, Display).
  target_link_libraries(client_platform PUBLIC uaro::core PRIVATE windowsapp)
elseif(CLIENT_CONSOLE_NATIVE)
  # Native console: the vendor backend under console/<vendor>/ supplies Platform/Window/fs +
  # ConsoleServices. These files carry `// TODO(SDK):` markers; the licensee links the vendor SDK
  # (the include dirs/libs come from the SDK toolchain file the "nx"/"prospero" preset points at).
  if(CLIENT_NX)
    set(_console_dir ${UARO_CLIENT_ROOT}/console/nx)
    set(_console_srcs ${_console_dir}/NxPlatform.cpp ${_console_dir}/NxConsoleServices.cpp)
  else()  # prospero (PS5) or orbis (PS4) — same PlayStation slot
    set(_console_dir ${UARO_CLIENT_ROOT}/console/prospero)
    set(_console_srcs ${_console_dir}/PsPlatform.cpp ${_console_dir}/PsConsoleServices.cpp)
  endif()
  add_library(client_platform STATIC
    ${_console_srcs}
    platform/ConsoleServices.cpp   # desktop file-backed impl kept as the default before install
  )
  # The console TUs include our headers ("platform/..", "core/..") AND their own (NxConsoleServices.hpp);
  # expose both the src root and the vendor dir. Vendor SDK includes/libs are injected by the toolchain.
  target_include_directories(client_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${_console_dir})
  target_link_libraries(client_platform PUBLIC uaro::core)
else()
  add_library(client_platform STATIC
    platform/sdl/SdlPlatform.cpp
    platform/ConsoleServices.cpp   # console-port prep: lifecycle/save/user, desktop = file-backed no-op
  )
  target_include_directories(client_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(client_platform PUBLIC uaro::core PRIVATE SDL3::SDL3)
endif()
add_library(uaro::platform ALIAS client_platform)

# render: bgfx implementation of the render abstraction
add_library(client_render STATIC
  render/RenderDevice.cpp
  render/SpriteBatch.cpp
  render/EmbeddedShaders.cpp
)
target_include_directories(client_render PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# bgfx is PUBLIC: render's public headers (SpriteBatch.hpp/Texture.hpp) expose
# bgfx types, so consumers (client_app) need bgfx's include directories too.
target_link_libraries(client_render PUBLIC uaro::core bgfx::bgfx bgfx::bx bgfx::bimg)
add_library(uaro::render ALIAS client_render)

# audio: SFX + BGM (#103) on plain SDL3 core audio + vendored dr_wav/dr_mp3 (single-header,
# public-domain decoders shipped in audio/). No SDL_mixer dependency: SDL3 (already required for
# the full build) is the only link. ON by default (S.: "делай чтобы собиралось со звуком"). With
# the option OFF the module compiles as a silent stub, so the client always builds either way.
option(CLIENT_WITH_AUDIO "Build with audio (SFX + BGM, SDL3 + dr_wav/dr_mp3)" ON)
add_library(client_audio STATIC audio/Audio.cpp audio/FsbVorbis.cpp audio/FsbVorbisTable.cpp)
target_include_directories(client_audio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(client_audio PUBLIC uaro::core uaro::resource uaro::formats)
if(CLIENT_WITH_AUDIO AND NOT CLIENT_XBOX AND NOT CLIENT_CONSOLE_NATIVE)  # Xbox/console: SDL audio absent -> silent stub for the first bring-up (vendor audio wired later)
  target_link_libraries(client_audio PRIVATE SDL3::SDL3)
  target_compile_definitions(client_audio PRIVATE CLIENT_WITH_AUDIO=1)
  # ROeM sound banks (#127): FSB5 ships Vorbis with the setup header stripped; libvorbis
  # (vcpkg) does the packet synthesis. Optional — without it RoM SFX silently fall back to wav.
  find_package(Vorbis CONFIG QUIET)
  if(TARGET Vorbis::vorbis)
    target_link_libraries(client_audio PRIVATE Vorbis::vorbis)
    target_compile_definitions(client_audio PRIVATE CLIENT_WITH_VORBIS=1)
  else()
    message(STATUS "uaRO audio: libvorbis not found - RoM FSB5 SFX disabled (wav fallback)")
  endif()
elseif(CLIENT_WITH_AUDIO AND CLIENT_XBOX)
  # Xbox / UWP: SDL3 dropped its WinRT backend, so there is no SDL audio device here. Drive the
  # in-box XAudio2 (xaudio2_9) instead -- same vendored dr_wav/dr_mp3/dr_flac decode, but XAudio2
  # source voices do the per-clip resample + mix + volume that SDL_AudioStream did on desktop. This
  # is what turns the "test package without sound" into a full one (S. 2026-07-29). No extra
  # capability is needed for playback; the BGM/wav packs already ship baked in the appx.
  target_compile_definitions(client_audio PRIVATE CLIENT_AUDIO_XAUDIO2=1)
  target_link_libraries(client_audio PRIVATE xaudio2)
endif()
add_library(uaro::audio ALIAS client_audio)

# ui: embedded bitmap font + immediate-mode widgets + original login skin loader
# (UiSkin pulls UI bitmaps through the VFS and decodes them with the BMP parser).
add_library(client_ui STATIC
  ui/Font.cpp
  ui/EmbeddedFont.cpp
  ui/EmbeddedFonts.cpp
  ui/Widgets.cpp
  ui/UiSkin.cpp
)
target_include_directories(client_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(client_ui PUBLIC uaro::core uaro::render uaro::resource uaro::formats)
# HarfBuzz: text shaping for complex scripts (Arabic joining, Thai marks, Devanagari conjuncts) + RTL.
# Optional -- without it Font falls back to the naive per-codepoint path (glyphs show but unshaped).
find_package(harfbuzz CONFIG QUIET)
if(TARGET harfbuzz::harfbuzz)
  target_link_libraries(client_ui PRIVATE harfbuzz::harfbuzz)
  target_compile_definitions(client_ui PRIVATE CLIENT_WITH_HARFBUZZ=1)
  message(STATUS "uaRO ui: HarfBuzz found -> complex-script shaping ENABLED")
else()
  message(STATUS "uaRO ui: HarfBuzz not found -> text shaping disabled (glyphs still render)")
endif()
add_library(uaro::ui ALIAS client_ui)

# The UI translation tables are BAKED into the exe via app/EmbeddedTexts.cpp (generated from
# assets/texts/*.cfg by tools/gen_embedded_texts.py). Regenerate it at build time whenever a .cfg
# changes, so editing a translation and rebuilding is enough — no manual codegen step (S.: "почему
# тексты не изменились" = a stale committed EmbeddedTexts.cpp). Guarded on Python: if the interpreter
# isn't found, fall back to the committed file as-is.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
  file(GLOB UARO_TEXT_CFGS "${UARO_CLIENT_ROOT}/assets/texts/*.cfg")
  add_custom_command(
    OUTPUT "${UARO_CLIENT_ROOT}/src/app/EmbeddedTexts.cpp"
    COMMAND ${Python3_EXECUTABLE} "${UARO_CLIENT_ROOT}/tools/gen_embedded_texts.py"
            "${UARO_CLIENT_ROOT}/assets/texts" "${UARO_CLIENT_ROOT}/src/app/EmbeddedTexts.cpp"
    DEPENDS ${UARO_TEXT_CFGS} "${UARO_CLIENT_ROOT}/tools/gen_embedded_texts.py"
    COMMENT "Regenerating embedded UI text tables from assets/texts/*.cfg"
    VERBATIM)
endif()

# app + game: lifecycle, loop, scenes (boot/login/char-select/in-game/map viewer)
add_library(client_app STATIC
  app/Application.cpp
  app/EmbeddedTexts.cpp
  app/EmbeddedClientInfo.cpp
  app/SceneStack.cpp
  game/BootScene.cpp
  game/MapRenderer.cpp
  game/ModelRenderer.cpp
  game/WaterRenderer.cpp
  game/ShadowRenderer.cpp
  game/LightGlowRenderer.cpp
  game/MapScene.cpp
  game/CharacterActor.cpp
  game/RomActor.cpp
  game/Gr2Models.cpp
  game/StrEffect.cpp
  game/ActorSprites.cpp
  game/AccessorySprites.cpp
  game/LoginScene.cpp
  game/CharSelectScene.cpp
  game/GameScene.cpp
  game/HomunAi.cpp
  game/RomBindings.cpp
  game/ViewerScene.cpp
  game/ViewerScene2D.cpp
  game/EffectConstList.cpp
  game/SkillConstList.cpp
  game/PatcherScene.cpp
)
target_include_directories(client_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(client_app PUBLIC uaro::core uaro::platform uaro::render uaro::resource uaro::formats uaro::world uaro::net uaro::ui uaro::audio uaro::patcher
  PRIVATE uaro::lua51)  # game/HomunAi.cpp embeds the Lua 5.1 VM for the homunculus AI (#148); resource links lua51 PRIVATE so its headers don't propagate here
add_library(uaro::app ALIAS client_app)

# MSIX/AppX package version, shared by the desktop MSIX and the Xbox UWP manifests (both configure_file
# it in as @_msix_version@). 4-part; the Store requires each new submission to be strictly higher, so
# bump the ONE value below to ship an update (S.: "обновить msix... одним числом").
# Channel scheme (S.): 0.0.x = Alpha, 0.1.x = Beta, 0.2.x = Pre-release, 0.3.x = Release. So 0.0.11 = Alpha 11.
#
# Deliberately a NORMAL variable, NOT a CACHE entry: a CACHE default is applied only on the FIRST
# configure of a build dir, so once win-msvc/CMakeCache.txt had cached 0.0.8.0 an edit here was ignored
# and the package kept building as 0.0.8 (S.: "почему msix создаётся с версией 8?"). A normal variable is
# re-read from this line every configure and shadows any stale cache. `unset(... CACHE)` clears a leftover
# entry from older trees so cmake-gui / an old cache can't resurrect the old number.
unset(CLIENT_MSIX_VERSION CACHE)
set(CLIENT_MSIX_VERSION "${UARO_VERSION}")  # single source of truth: Client/VERSION

# executable (desktop) / shared lib (Android: SDL's Java activity dlopen's libmain.so and calls
# SDL_main). Everything below (shader compile/embed, links) works for either target kind.
if(CLIENT_XBOX)
  # UWP app: the WinRT IFrameworkView entry (uwp/App.cpp) replaces main.cpp; the SDL loop is gone.
  add_executable(UaRO ${UARO_CLIENT_ROOT}/uwp/App.cpp)
  set_target_properties(UaRO PROPERTIES OUTPUT_NAME "BornRok")  # BornRok.exe inside the .msix (matches manifest Executable)
  target_link_libraries(UaRO PRIVATE windowsapp)
  # Package manifest (device families incl. Windows.Xbox). Attaching it makes CMake+VS build the .appx
  # when CMAKE_SYSTEM_NAME=WindowsStore. Logo assets it references live in uwp/Assets/ and must be
  # packaged as deployment content into the appx's Assets/ folder (else APPX0703 "not part of payload").
  # Version + arch come from the build (CLIENT_MSIX_VERSION) via configure_file, same as the desktop MSIX.
  set(_msix_version "${CLIENT_MSIX_VERSION}")
  set(_msix_arch "x64")  # Xbox is x64
  configure_file(${UARO_CLIENT_ROOT}/uwp/Package.appxmanifest.in
                 ${CMAKE_CURRENT_BINARY_DIR}/Package.appxmanifest @ONLY)
  target_sources(UaRO PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/Package.appxmanifest)
  file(GLOB UWP_ASSETS ${UARO_CLIENT_ROOT}/uwp/Assets/*.png)
  target_sources(UaRO PRIVATE ${UWP_ASSETS})
  set_source_files_properties(${UWP_ASSETS} PROPERTIES
    VS_DEPLOYMENT_CONTENT 1
    VS_DEPLOYMENT_LOCATION "Assets")
  # Bake the ENTIRE Client/content/ folder into the appx payload under content/, same as the
  # Steam/desktop-MSIX packages — Xbox has no patcher so the content MUST ship in the package (S.
  # 2026-07-27; "пакет должен собираться с содержимым папки контент" 2026-07-29). The client mounts
  # base_dir()/content at runtime and reads content/data.ini for the pack load order, so we ship
  # every file there (the .zip/.grf packs AND data.ini + any loose data), not just the archives.
  # LIST_DIRECTORIES false so a stray subdir isn't added as a source. content/ is flat (the patcher
  # writes flat filenames), so a single flat VS_DEPLOYMENT_LOCATION "content" reproduces it exactly.
  # GLOB'd at configure time, so (re)configure after refreshing Client/content/.
  file(GLOB UWP_CONTENT LIST_DIRECTORIES false ${UARO_CLIENT_ROOT}/content/*)
  if(UWP_CONTENT)
    target_sources(UaRO PRIVATE ${UWP_CONTENT})
    set_source_files_properties(${UWP_CONTENT} PROPERTIES
      VS_DEPLOYMENT_CONTENT 1
      VS_DEPLOYMENT_LOCATION "content")
  endif()
  set_target_properties(UaRO PROPERTIES
    WIN32_EXECUTABLE TRUE  # /SUBSYSTEM:WINDOWS -> wWinMainCRTStartup calls our wWinMain (not main)
    VS_GLOBAL_DefaultLanguage "en-US"
    VS_GLOBAL_MinimalCoreWin "true"
    # Produce the .msix on a normal build by default (S.: "чтобы msix собирались в дефолте в хбокс -- как
    # x64"), unsigned (the Store / Xbox Dev Mode signs it). Parity with the desktop `msix` ALL target.
    VS_GLOBAL_GenerateAppxPackageOnBuild "true"
    VS_GLOBAL_AppxPackageSigningEnabled "false"
    VS_GLOBAL_AppxBundle "Never")
elseif(CLIENT_CONSOLE_NATIVE)
  # Native console: the vendor entry replaces main.cpp. NX uses nnMain() (in NxMain.cpp); PlayStation
  # uses a standard main() (in PsMain.cpp). The rest of the target (shader embed, links) is shared.
  if(CLIENT_NX)
    add_executable(UaRO ${UARO_CLIENT_ROOT}/console/nx/NxMain.cpp)
  else()  # prospero / orbis
    add_executable(UaRO ${UARO_CLIENT_ROOT}/console/prospero/PsMain.cpp)
  endif()
elseif(ANDROID)
  add_library(UaRO SHARED main.cpp)
  set_target_properties(UaRO PROPERTIES OUTPUT_NAME "main")  # -> libmain.so
else()
  add_executable(UaRO main.cpp)
  # Ship the desktop binary under the brand name (S.: "выходные файлы ехе... с новым названием"):
  # BornRok.exe on Windows, BornRok on Linux/macOS. The CMake TARGET stays `UaRO` (referenced all
  # over this file); only the produced filename changes via OUTPUT_NAME.
  set_target_properties(UaRO PROPERTIES OUTPUT_NAME "BornRok")
  if(WIN32)
    # No black console window on Windows (S.: "убери чёрную консоль, открывалось только окно игры").
    # Build as a GUI app (WINDOWS subsystem) but keep the ordinary int main() entry via mainCRTStartup
    # so no WinMain is needed. The log now goes to output_log.txt (there's no console to print to).
    set_target_properties(UaRO PROPERTIES WIN32_EXECUTABLE TRUE)
    if(MSVC)
      target_link_options(UaRO PRIVATE "/ENTRY:mainCRTStartup")
    else()  # MinGW
      target_link_options(UaRO PRIVATE "-mwindows")
    endif()
  endif()
endif()
target_link_libraries(UaRO PRIVATE uaro::app)
if(ANDROID)
  # bgfx's GL backend on Android creates its context via EGL (eglGetDisplay, ...); the vcpkg bgfx
  # package doesn't pull libEGL, so link it (and GLESv2) explicitly or libmain.so is undefined. (S.)
  target_link_libraries(UaRO PRIVATE EGL GLESv2)
endif()

if(APPLE)
  # bgfx's Metal backend + SDL3 need these system frameworks. vcpkg's ports usually propagate
  # them, but link the render-critical ones explicitly so a static-SDL or trimmed vcpkg build
  # doesn't fail at link with "Undefined symbols ... _OBJC_CLASS_$_CAMetalLayer" etc.
  if(IOS)
    # iOS/iPadOS: UIKit (not Cocoa); CMake sets IOS=TRUE when CMAKE_SYSTEM_NAME=iOS.
    target_link_libraries(UaRO PRIVATE
      "-framework Metal" "-framework QuartzCore" "-framework UIKit"
      "-framework CoreGraphics" "-framework CoreMotion" "-framework AVFoundation")
    # iOS apps are ALWAYS a signed .app bundle -- always on (no option). Uses the iOS Info.plist
    # (device family / orientations / required Metal capability). Codesign is set at build time
    # via -DCMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM=<TEAMID> (a Mac + Apple Developer account).
    set_target_properties(UaRO PROPERTIES
      MACOSX_BUNDLE TRUE
      MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/../ios/Info.plist.in"
      MACOSX_BUNDLE_BUNDLE_NAME "BornRok"
      MACOSX_BUNDLE_GUI_IDENTIFIER "ua.bornrok.client"
      MACOSX_BUNDLE_BUNDLE_VERSION "${UARO_VERSION}"
      MACOSX_BUNDLE_SHORT_VERSION_STRING "${UARO_VERSION}"
      XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"  # 1=iPhone, 2=iPad
      XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Automatic")
  else()
    # macOS desktop: Cocoa/IOKit/CoreVideo.
    target_link_libraries(UaRO PRIVATE
      "-framework Metal" "-framework QuartzCore" "-framework Cocoa"
      "-framework IOKit" "-framework CoreVideo")
    # Optional .app bundle for distribution (Info.plist + later .icns/sign). OFF by default so the
    # normal mac build is a plain executable that finds its assets beside it, exactly like Linux.
    # Assets are also embedded in the binary, so either way the UI starts without loose files.
    option(CLIENT_MACOS_BUNDLE "Build BornRok as a macOS .app bundle" OFF)
    if(CLIENT_MACOS_BUNDLE)
      set_target_properties(UaRO PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/../macos/Info.plist.in"
        MACOSX_BUNDLE_BUNDLE_NAME "BornRok"
        MACOSX_BUNDLE_GUI_IDENTIFIER "ua.bornrok.client"
        MACOSX_BUNDLE_BUNDLE_VERSION "${UARO_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${UARO_VERSION}"
        MACOSX_BUNDLE_INFO_STRING "BornRok client")
    endif()
  endif()
endif()

# Ship runtime assets next to the executable. Fonts (NotoSans.ttf) are rasterized
# at startup by ui/Font; copied like the compiled shaders so the client finds them
# under <exe dir>/fonts/. base_dir() (SDL_GetBasePath) resolves to this directory.
add_custom_command(TARGET UaRO POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${UARO_CLIENT_ROOT}/assets/fonts" "$<TARGET_FILE_DIR:UaRO>/fonts"
  COMMENT "Copying fonts next to UaRO"
  VERBATIM)

# UI text tables (i18n, #104): texts/<Language>.cfg next to the exe; base_dir() finds them.
add_custom_command(TARGET UaRO POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${UARO_CLIENT_ROOT}/assets/texts" "$<TARGET_FILE_DIR:UaRO>/texts"
  COMMENT "Copying UI text tables next to UaRO"
  VERBATIM)

# Compile bgfx shaders (.sc -> .bin) next to the executable, if shaderc found.
# Disable with -DCLIENT_BUILD_SHADERS=OFF to always build the window (clear-only).
# Android is EXCLUDED: vcpkg's bgfx::shaderc (and even find_program, which picks up vcpkg's tools dir on
# PATH) is the arm64-android binary and can't run on the build host -> shaderc fails. Cross-compiling
# needs a HOST shaderc; until that's wired, the Android build ships without shaders (clear-only) so the
# APK still builds + launches. TODO: host shaderc -> essl(GLES3) shaders + real rendering. (S.)
if(CLIENT_BUILD_SHADERS AND (NOT ANDROID OR CLIENT_HOST_SHADERC))
  include(CompileShaders)
  client_compile_shaders(UaRO
    SHADERS
      ${UARO_CLIENT_ROOT}/assets/shaders/vs_sprite.sc
      ${UARO_CLIENT_ROOT}/assets/shaders/fs_sprite.sc
    VARYING ${UARO_CLIENT_ROOT}/assets/shaders/varying.def.sc
  )
  # Bake the sprite shaders into the exe so the patcher UI renders from a bare exe (S.).
  client_embed_sprite_shaders(UaRO)
  client_compile_shaders(UaRO
    SHADERS
      ${UARO_CLIENT_ROOT}/assets/shaders/vs_ground.sc
      ${UARO_CLIENT_ROOT}/assets/shaders/fs_ground.sc
    VARYING ${UARO_CLIENT_ROOT}/assets/shaders/varying_ground.def.sc
  )
  client_compile_shaders(UaRO
    SHADERS
      ${UARO_CLIENT_ROOT}/assets/shaders/vs_model.sc
      ${UARO_CLIENT_ROOT}/assets/shaders/fs_model.sc
    VARYING ${UARO_CLIENT_ROOT}/assets/shaders/varying_model.def.sc
  )
  # Billboard sprite shaders (vs_model + a depth bias; fs_model + a fade alpha so a
  # dying actor's corpse can dissolve instead of freezing).
  client_compile_shaders(UaRO
    SHADERS
      ${UARO_CLIENT_ROOT}/assets/shaders/vs_sprite3d.sc
      ${UARO_CLIENT_ROOT}/assets/shaders/fs_sprite3d.sc
    VARYING ${UARO_CLIENT_ROOT}/assets/shaders/varying_model.def.sc
  )
  # Skinned RoM models (ROeM mobs): 4-bone LBS with a CPU-built bone palette.
  client_compile_shaders(UaRO
    SHADERS
      ${UARO_CLIENT_ROOT}/assets/shaders/vs_romskin.sc
      ${UARO_CLIENT_ROOT}/assets/shaders/fs_romskin.sc
    VARYING ${UARO_CLIENT_ROOT}/assets/shaders/varying_romskin.def.sc
  )
  # Lit sprites (S.: normal maps on sprites like models/ground get): fs_spritelit pairs
  # with the SAME vs_sprite3d (identical varyings -- no DX11 link trap); the normal map is
  # generated per frame from the sprite's own luminance, the sun dir arrives as a uniform.
  client_compile_shaders(UaRO
    SHADERS
      ${UARO_CLIENT_ROOT}/assets/shaders/fs_spritelit.sc
    VARYING ${UARO_CLIENT_ROOT}/assets/shaders/varying_model.def.sc
  )
  # Ground decals: the hovered-cell cursor and the walk-path line (flat colour).
  client_compile_shaders(UaRO
    SHADERS
      ${UARO_CLIENT_ROOT}/assets/shaders/vs_overlay.sc
      ${UARO_CLIENT_ROOT}/assets/shaders/fs_overlay.sc
    VARYING ${UARO_CLIENT_ROOT}/assets/shaders/varying_overlay.def.sc
  )
  # Dungeon light glows (#117 part B): additive radial billboard per RSW point light.
  client_compile_shaders(UaRO
    SHADERS
      ${UARO_CLIENT_ROOT}/assets/shaders/vs_glow.sc
      ${UARO_CLIENT_ROOT}/assets/shaders/fs_glow.sc
    VARYING ${UARO_CLIENT_ROOT}/assets/shaders/varying_glow.def.sc
  )
  # HDR tonemap + FSR1 (EASU/RCAS) post-process (#111): all share the fullscreen triangle.
  client_compile_shaders(UaRO
    SHADERS
      ${UARO_CLIENT_ROOT}/assets/shaders/vs_fullscreen.sc
      ${UARO_CLIENT_ROOT}/assets/shaders/fs_tonemap.sc
      ${UARO_CLIENT_ROOT}/assets/shaders/fs_fsr_easu.sc
      ${UARO_CLIENT_ROOT}/assets/shaders/fs_fsr_rcas.sc
      ${UARO_CLIENT_ROOT}/assets/shaders/fs_grade.sc
      ${UARO_CLIENT_ROOT}/assets/shaders/fs_godray.sc
      ${UARO_CLIENT_ROOT}/assets/shaders/fs_vollight.sc
    VARYING ${UARO_CLIENT_ROOT}/assets/shaders/varying_fs.def.sc
  )
  # Bake EVERY shader into the exe (S.: "в ехе зашей все шейдеры") so the client renders fully from a
  # single file with no external shaders/ folder. The sprite pair is already embedded above; add the
  # rest. Must be AFTER all client_compile_shaders() calls (it depends on their compiled .bin).
  client_embed_shaders(UaRO
    vs_ground fs_ground vs_model fs_model vs_sprite3d fs_sprite3d vs_romskin fs_romskin
    fs_spritelit vs_overlay fs_overlay vs_glow fs_glow
    vs_fullscreen fs_tonemap fs_fsr_easu fs_fsr_rcas fs_grade fs_godray fs_vollight)
endif()

# ---------------------------------------------------------------------------
# Second, AVX2-optimised executable: BornRok-avx2.exe (S.: "при компиляции
# создавалось 2 ехе, второй с avx2"). The baseline UaRO above stays scalar so it
# keeps running on CPUs without AVX2 (S. earlier: no AVX in the default build);
# this variant recompiles every first-party translation unit with /arch:AVX2.
#
# It is additive and self-contained: it does NOT reuse the static libs (those are
# built at the baseline ISA), it compiles all first-party sources directly so the
# whole binary gets AVX2, and it links only the shared third-party deps (SDL3 /
# bgfx / zlib, which we can't re-arch from vcpkg anyway). Both exes land in the
# same output dir, so the fonts/texts/shaders copied next to UaRO serve both.
option(CLIENT_BUILD_AVX2 "Also build the AVX2-optimised BornRok-avx2 executable" ON)
# AVX2 is an x86/x64-only ISA -- there is no AVX2 on ARM (Windows-on-ARM, arm64 linux).
# Detect the TARGET arch and skip the whole second exe elsewhere; MSVC otherwise warns
# "cl D9002: пропуск неизвестного параметра /arch:AVX2" for an ARM64 compile (S. 2026-07-08).
set(CLIENT_TARGET_X86 OFF)
if(CMAKE_GENERATOR_PLATFORM)  # multi-config VS: trust the -A platform (x64 / Win32 / ARM64)
  if(CMAKE_GENERATOR_PLATFORM MATCHES "^(x64|Win32)$")
    set(CLIENT_TARGET_X86 ON)
  endif()
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "(x86_64|AMD64|amd64|i[3-6]86|x86)")
  set(CLIENT_TARGET_X86 ON)
endif()
if(CLIENT_BUILD_AVX2 AND NOT ANDROID AND APPLE)
  message(STATUS "AVX2 exe skipped on macOS (ship one binary; ideally a universal arm64+x86_64).")
endif()
if(CLIENT_BUILD_AVX2 AND NOT ANDROID AND NOT APPLE AND NOT CLIENT_TARGET_X86)
  message(STATUS "AVX2 exe skipped: target arch is not x86/x64 (no AVX2 on ARM).")
endif()
# The 2nd AVX2 exe is a Windows/Linux distribution optimisation; skip it on Android (single .so),
# on ARM (no AVX2), and on macOS (we ship one binary, ideally a universal arm64+x86_64).
if(CLIENT_XBOX OR CLIENT_CONSOLE_NATIVE)
  message(STATUS "AVX2 exe skipped on console (single package; vendor entry, not main.cpp).")
endif()
if(CLIENT_BUILD_AVX2 AND NOT ANDROID AND NOT APPLE AND CLIENT_TARGET_X86 AND NOT CLIENT_XBOX AND NOT CLIENT_CONSOLE_NATIVE)
  # The AVX2 exe must recompile every source with /arch:AVX2 (it can't reuse the non-AVX2 static-lib
  # objects), but it must NOT maintain a duplicate file list -- that drift is exactly what dropped a new
  # scene from the avx2 build (S.: "зачем ведёшь другую кодовую базу?"). Pull each module's sources
  # straight from its static library's SOURCES property, so a file added to any client_* lib (including
  # conditional target_sources like MapLoader/Http/PatchRunner) is automatically in the AVX2 build too.
  # Only the compile flags differ between the two targets, never the file list.
  add_executable(UaRO_avx2
    main.cpp
    $<TARGET_PROPERTY:client_core,SOURCES>
    $<TARGET_PROPERTY:client_resource,SOURCES>
    $<TARGET_PROPERTY:client_formats,SOURCES>
    $<TARGET_PROPERTY:client_world,SOURCES>
    $<TARGET_PROPERTY:client_net,SOURCES>
    $<TARGET_PROPERTY:client_patcher,SOURCES>
    $<TARGET_PROPERTY:client_platform,SOURCES>
    $<TARGET_PROPERTY:client_render,SOURCES>
    $<TARGET_PROPERTY:client_audio,SOURCES>
    $<TARGET_PROPERTY:client_ui,SOURCES>
    $<TARGET_PROPERTY:client_app,SOURCES>
  )
  set_target_properties(UaRO_avx2 PROPERTIES OUTPUT_NAME "BornRok-avx2")
  target_compile_features(UaRO_avx2 PRIVATE cxx_std_20)
  target_include_directories(UaRO_avx2 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(UaRO_avx2 PRIVATE
    Threads::Threads ZLIB::ZLIB SDL3::SDL3 bgfx::bgfx bgfx::bx bgfx::bimg CURL::libcurl uaro::lua51)
  if(WIN32)
    target_link_libraries(UaRO_avx2 PRIVATE ws2_32)
    # No console window (S.): GUI subsystem, keep int main() via mainCRTStartup. Same as the base exe.
    set_target_properties(UaRO_avx2 PROPERTIES WIN32_EXECUTABLE TRUE)
    if(MSVC)
      target_link_options(UaRO_avx2 PRIVATE "/ENTRY:mainCRTStartup")
    else()
      target_link_options(UaRO_avx2 PRIVATE "-mwindows")
    endif()
  endif()
  if(CLIENT_WITH_AUDIO)
    target_compile_definitions(UaRO_avx2 PRIVATE CLIENT_WITH_AUDIO=1)
    if(TARGET Vorbis::vorbis)  # FSB5 RoM sound banks, same wiring as client_audio (#127)
      target_link_libraries(UaRO_avx2 PRIVATE Vorbis::vorbis)
      target_compile_definitions(UaRO_avx2 PRIVATE CLIENT_WITH_VORBIS=1)
    endif()
  endif()
  # The AVX2 exe compiles formats/UnityFs.cpp from its OWN source list (not via
  # client_formats), so it needs the zstd define + link too — without this RoM bundles
  # fail with 'built without CLIENT_WITH_ZSTD' in the avx2 build only (S.'s log).
  if(UARO_ZSTD_TARGET)
    target_compile_definitions(UaRO_avx2 PRIVATE CLIENT_WITH_ZSTD=1)
    target_link_libraries(UaRO_avx2 PRIVATE ${UARO_ZSTD_TARGET})
  elseif(UARO_ZSTD_LIB AND UARO_ZSTD_INC)
    target_compile_definitions(UaRO_avx2 PRIVATE CLIENT_WITH_ZSTD=1)
    target_include_directories(UaRO_avx2 PRIVATE ${UARO_ZSTD_INC})
    target_link_libraries(UaRO_avx2 PRIVATE ${UARO_ZSTD_LIB})
  endif()
  # The AVX2 exe compiles formats/Webp.cpp + resource/Vfs.cpp from its OWN source list (not via the
  # client_formats/client_resource libs), so it needs the webp define + link too -- otherwise .webp
  # overrides are IGNORED in the AVX2 build ONLY, while the scalar BornRok.exe honours them (S.: "почему
  # АВХ2 без вебП? это твоя недоработка!"). Mirror the client_formats/client_resource wiring above.
  if(UARO_WEBP_TARGET)
    target_compile_definitions(UaRO_avx2 PRIVATE CLIENT_WITH_WEBP=1)
    target_link_libraries(UaRO_avx2 PRIVATE ${UARO_WEBP_TARGET})
    if(UARO_WEBP_MANUAL_INC)
      target_include_directories(UaRO_avx2 PRIVATE ${UARO_WEBP_MANUAL_INC})
    endif()
  endif()
  # HarfBuzz shaping for the AVX2 exe too (parity with client_ui). Optional.
  if(TARGET harfbuzz::harfbuzz)
    target_compile_definitions(UaRO_avx2 PRIVATE CLIENT_WITH_HARFBUZZ=1)
    target_link_libraries(UaRO_avx2 PRIVATE harfbuzz::harfbuzz)
  endif()
  # The AVX2 switch itself.
  if(MSVC)
    target_compile_options(UaRO_avx2 PRIVATE /arch:AVX2)
  else()
    target_compile_options(UaRO_avx2 PRIVATE -mavx2 -mfma)
  endif()
  # Bake ALL shaders into the AVX2 exe too (reusing UaRO's compiled .bin), so it is equally a
  # single self-contained file (S.: "в ехе зашей все шейдеры").
  if(CLIENT_BUILD_SHADERS AND TARGET UaRO)
    client_embed_sprite_shaders(UaRO_avx2 UaRO)
    client_embed_shaders(UaRO_avx2
      vs_ground fs_ground vs_model fs_model vs_sprite3d fs_sprite3d vs_romskin fs_romskin
      fs_spritelit vs_overlay fs_overlay vs_glow fs_glow
      vs_fullscreen fs_tonemap fs_fsr_easu fs_fsr_rcas fs_grade fs_godray fs_vollight)
  endif()
endif()

# ---------------------------------------------------------------------------
# Windows desktop MSIX packaging (S.: "под виндовс тоже собирались msix пакеты").
# Opt-in `msix` target: wraps the static BornRok.exe as a Desktop-Bridge MSIX (full-trust Win32 app).
# NOTE: MSIX always installs to the managed WindowsApps location -- a custom path (C:\games\uaro) is
# NOT possible with MSIX. The produced package is UNSIGNED; sign it with win/sign-msix.ps1 before
# install (see docs/windows-msix-km.md). Desktop only (the Xbox build already makes its own appx).
# ---------------------------------------------------------------------------
if(MSVC AND NOT CLIENT_XBOX)
  # ON by default (S.: "пусть по дефолту сразу собирает с пакетами"): the msix builds as part of the
  # normal build when makeappx is available. Covers x64 AND arm64 (S.: "для АРМ тоже нужно").
  option(CLIENT_WIN_MSIX "Build a desktop MSIX package alongside the exe" ON)
  if(CLIENT_WIN_MSIX AND TARGET UaRO)
    # Manifest ProcessorArchitecture must match the build (x64 / arm64), else the package won't
    # deploy. Configure it from the VS platform.
    if(CMAKE_GENERATOR_PLATFORM MATCHES "ARM64|arm64" OR CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|arm64|aarch64")
      set(_msix_arch "arm64")
    else()
      set(_msix_arch "x64")
    endif()
    set(_msix_version "${CLIENT_MSIX_VERSION}")
    # Store products are per-arch: S. registered the arm64 desktop build as its OWN Partner Center
    # product "BornRok-ARM", so the manifest Package/Identity Name + Properties/DisplayName MUST match
    # that reservation or the Store rejects the upload ("Недопустимое имя идентификатора пакета",
    # 2026-07-29). Defaults follow S.'s reservations per arch; override for any other listing (e.g. an
    # x64 Store product once reserved) via -DCLIENT_MSIX_IDENTITY_NAME=... -DCLIENT_MSIX_DISPLAY_NAME=...
    # Each arch configures in its own build dir (build/win-arm64 vs build/win-msvc), so the per-arch
    # default lands in the right cache. (Publisher CN is shared -> same package-family hash.)
    if(_msix_arch STREQUAL "arm64")
      set(_default_msix_identity "PechSoft.BornRok-ARM")
      set(_default_msix_display  "BornRok-ARM")
    else()
      set(_default_msix_identity "PechSoft.UaRO")  # x64 desktop Store name TBD -> override when reserved
      set(_default_msix_display  "BornRok")
    endif()
    set(CLIENT_MSIX_IDENTITY_NAME "${_default_msix_identity}" CACHE STRING
        "MSIX Package/Identity Name (must match the Partner Center reservation for this product)")
    set(CLIENT_MSIX_DISPLAY_NAME "${_default_msix_display}" CACHE STRING
        "MSIX Package/Properties/DisplayName (must match the reserved product name)")
    set(_msix_identity_name "${CLIENT_MSIX_IDENTITY_NAME}")
    set(_msix_display_name "${CLIENT_MSIX_DISPLAY_NAME}")
    configure_file(${UARO_CLIENT_ROOT}/win/Package.appxmanifest.in
                   ${CMAKE_BINARY_DIR}/AppxManifest.xml @ONLY)
    # Locate makeappx.exe. A "Developer Command Prompt for VS" puts it on PATH, but a plain BuildTools
    # shell does NOT (S. hit MSB8066 / exit 9009 = command not found -> msix couldn't run). So also
    # search the Windows 10/11 SDK bin by ABSOLUTE path, newest SDK version first, so the default build
    # works from any terminal. makeappx is a host tool -> look under the host-arch (x64/x86) subfolders.
    # ONLY the x64 (64-bit) makeappx — x32 is never used (S. 2026-07-27 "х32 нам не нужен совсем").
    # The x86 makeappx is a 32-bit process capped at ~4GB address space and OOMs ("0x8007000E Package
    # creation failed") on a multi-GB bundled-content package (a single 2.9GB pack overflows its address
    # space regardless of physical RAM — S. had 8GB free RAM + 200GB disk, still failed). So we glob x64
    # bin dirs exclusively; find_program can never fall back to the 32-bit tool.
    file(GLOB _makeappx_dirs LIST_DIRECTORIES true
      "$ENV{ProgramFiles\(x86\)}/Windows Kits/10/bin/*/x64"
      "$ENV{ProgramFiles\(x86\)}/Windows Kits/10/bin/x64"
      "$ENV{ProgramFiles}/Windows Kits/10/bin/*/x64"
      "$ENV{ProgramW6432}/Windows Kits/10/bin/*/x64")
    list(SORT _makeappx_dirs)      # versioned dirs sort ascending...
    list(REVERSE _makeappx_dirs)   # ...reverse so the newest SDK is searched first
    # find_program CACHES its result in CMakeCache.txt, so a plain reconfigure keeps a previously-resolved
    # (possibly x86) path and this x64-only logic would NOT re-run. Clear it every configure so the path is
    # always freshly resolved from the globs below.
    unset(MAKEAPPX_EXE CACHE)
    # NB: NAMES_PER_DIR keeps HINTS priority (newest-SDK x64) ahead of the ENV fallbacks, which could
    # otherwise point at an x86 bin.
    find_program(MAKEAPPX_EXE NAMES makeappx NAMES_PER_DIR
                 HINTS ${_makeappx_dirs} ENV WindowsSdkVerBinPath ENV WindowsSdkBinPath)
    set(_msix_layout "${CMAKE_BINARY_DIR}/msix-layout")
    set(_msix_out "${CMAKE_BINARY_DIR}/BornRok.msix")
    # The exe embeds fonts/texts/shaders/clientinfo, so the layout only needs the exe + logos + the
    # manifest (named AppxManifest.xml at the package root, as makeappx expects).
    set(_msix_cmds
      COMMAND ${CMAKE_COMMAND} -E rm -rf "${_msix_layout}"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${_msix_layout}"
      COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:UaRO>" "${_msix_layout}/BornRok.exe"
      COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_BINARY_DIR}/AppxManifest.xml" "${_msix_layout}/AppxManifest.xml"
      COMMAND ${CMAKE_COMMAND} -E copy_directory "${UARO_CLIENT_ROOT}/uwp/Assets" "${_msix_layout}/Assets"
      # Bake the game-data packs into the MSIX just like the Steam/console packages (S. 2026-07-27:
      # "контент в MSIX собирался вместе с пакетом"). Client/content/*.zip|.grf are copied to
      # <pkg>/content/, and the client mounts every pack there at runtime (Application mountGameData
      # scans base_dir()/content). On a read-only MSIX install that's the baked, no-patcher content;
      # the writable data_dir() still takes patcher downloads/event packs on top. The tracked
      # README.md/.gitkeep are dropped so they don't ship. If Client/content/ holds only those, the
      # package ships no data and the in-client patcher fetches on first run (same as loose desktop).
      COMMAND ${CMAKE_COMMAND} -E copy_directory "${UARO_CLIENT_ROOT}/content" "${_msix_layout}/content"
      COMMAND ${CMAKE_COMMAND} -E rm -f "${_msix_layout}/content/README.md" "${_msix_layout}/content/.gitkeep"
      # /nc = do NOT compress. The content packs are already ZIP-compressed multi-GB files; letting
      # makeappx re-compress them exhausts memory -> "0x8007000E Package creation failed" (S. 2026-07-27).
      # Storing them as-is is both correct (no gain from re-compressing) and avoids the OOM. The MSIX
      # ends up ~= the content size; that's expected for a bundled-data package.
      COMMAND "${MAKEAPPX_EXE}" pack /o /nc /d "${_msix_layout}" /p "${_msix_out}")
    if(MAKEAPPX_EXE)
      message(STATUS "CLIENT_WIN_MSIX: using makeappx: ${MAKEAPPX_EXE} (${_msix_arch})")
      # Part of the default build (ALL) so `cmake --build` produces BornRok.msix automatically -- works from
      # any terminal now that MAKEAPPX_EXE is an absolute SDK path, not a bare PATH lookup.
      add_custom_target(msix ALL ${_msix_cmds} DEPENDS UaRO
        COMMENT "Packaging BornRok.exe -> ${_msix_out} (${_msix_arch}, UNSIGNED; sign with win/sign-msix.ps1)"
        VERBATIM)
    else()
      # makeappx.exe genuinely not present (Windows SDK not installed). Don't break the normal build and
      # don't bake a bogus command into a target -- warn now, and make `msix` fail loudly with guidance.
      message(WARNING "CLIENT_WIN_MSIX: makeappx.exe not found (searched PATH + Windows Kits\\10\\bin). "
                      "Install the Windows 10/11 SDK (component 'MSIX Packaging Tools' / SDK), then "
                      "reconfigure. MSIX will NOT be built until then.")
      add_custom_target(msix
        COMMAND ${CMAKE_COMMAND} -E false
        COMMENT "MSIX not built: makeappx.exe not found. Install the Windows SDK and reconfigure." VERBATIM)
    endif()
  endif()
endif()
