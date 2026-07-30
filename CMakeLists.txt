cmake_minimum_required(VERSION 3.24)
cmake_policy(SET CMP0091 NEW)  # MSVC runtime selection via CMAKE_MSVC_RUNTIME_LIBRARY

# SINGLE SOURCE OF TRUTH for the client version (S.: "версия из одной переменной, вид 0.0.11.0").
# Bump Client/VERSION only — every consumer derives from UARO_VERSION: project() below, the MSIX/macOS
# package versions (src/CMakeLists.txt), the -DUARO_VERSION_STR window title, and the Android
# versionName/versionCode (android/app/build.gradle reads the same file).
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" UARO_VERSION)
string(STRIP "${UARO_VERSION}" UARO_VERSION)  # -> "0.0.11.0"
project(UaRO LANGUAGES C CXX VERSION ${UARO_VERSION})  # C for vendored Lua 5.1 (third_party/lua51)
# Baked into every C++ TU so the window title (main.cpp) shows the version from this one place.
add_compile_definitions(UARO_VERSION_STR="${UARO_VERSION}")

# Client source root. Equals CMAKE_SOURCE_DIR on desktop, but NOT on Android where this Client tree is
# add_subdirectory'd under android/jni (so CMAKE_SOURCE_DIR = android/jni). Use this for all Client
# asset paths (shaders/fonts/texts) so they resolve in both builds.
set(UARO_CLIENT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}" CACHE INTERNAL "uaRO Client source root")

# Fully static executables (S.: "все библиотеки статично линковались в exe"):
#   - Windows: vcpkg static triplet (x64-windows-static, set in the preset) + static CRT
#     (/MT) -> no VC++ redistributable needed on testers' machines.
#   - Linux: vcpkg libs are static already; additionally pin libstdc++/libgcc into the
#     binary below (glibc/X11/GL stay dynamic -- truly static GUI binaries aren't viable).
if(MSVC)
  # UWP/Xbox forbids the static CRT (/MT); its app container + the x64-uwp vcpkg triplet mandate the
  # dynamic CRT (/MD). Leave CMAKE_MSVC_RUNTIME_LIBRARY UNSET there so CMake uses its default
  # (MultiThreaded$<CONFIG:Debug:Debug>DLL = dynamic). Desktop forces the static CRT for single-file exes.
  if(NOT CLIENT_CONSOLE_BACKEND STREQUAL "xbox")
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  endif()
endif()
if(UNIX AND NOT APPLE)
  add_link_options(-static-libstdc++ -static-libgcc)
endif()

# ---------------------------------------------------------------------------
# uaRO cross-platform client (v0 "Foundation")
#   - core/        : zero external deps -> always buildable, unit-tested
#   - platform/    : SDL3-backed window/input/filesystem
#   - render/      : bgfx-backed rendering
#   - app/ game/   : loop + scenes
# Pass -DCLIENT_CORE_ONLY=ON to build just the dependency-free core + tests
# (used for fast CI and offline verification; SDL3/bgfx are not required).
# ---------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)  # default to an optimized build (S.)
endif()

option(CLIENT_CORE_ONLY "Build only the dependency-free core library and its tests" OFF)
option(CLIENT_BUILD_TESTS "Build unit tests" ON)
option(CLIENT_BUILD_SHADERS "Compile bgfx shaders next to the client (needs shaderc)" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8 /EHsc)
  # The GCC/Clang build is clean at -Wall -Wextra -Wpedantic; MSVC /W4 adds a wall of benign,
  # off-by-default-elsewhere noise. Silence those equivalents so "clean build" stays meaningful:
  #   C4244/C4267 narrowing (GCC/Clang -Wconversion is off by default) -- pervasive in the data
  #               tables (job/skill/item ids) and mesh maths;
  #   C4125 a decimal digit ending an octal escape (the EUC-KR resource-path string literals);
  #   C4127 constant conditional (from the CHECK() test macros);
  #   C4996 'unsafe' fopen/getenv/sscanf (we use the standard C calls deliberately).
  # Genuinely useful warnings (shadowing C4456/C4458, ...) stay on; the vendored Gr2Oodle.cpp
  # silences its own with a local pragma.
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
  add_compile_options(/wd4244 /wd4267 /wd4125 /wd4127)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_subdirectory(third_party/lua51)  # vendored Lua 5.1 for loading RO .lub data tables from the GRF
add_subdirectory(src)

# Host CLI utilities + tests are desktop-only: on Xbox/UWP they'd build as AppX packages, and on the
# native consoles (nx/prospero/orbis) they can't link (no console main / SDK CLI runtime), so skip
# them on ANY console backend. CLIENT_CONSOLE_BACKEND empty = desktop -> tools/tests build normally.
if(TARGET uaro::resource AND NOT CLIENT_CONSOLE_BACKEND)
  add_subdirectory(tools)
endif()

if(CLIENT_BUILD_TESTS AND NOT CLIENT_CONSOLE_BACKEND)
  enable_testing()
  add_subdirectory(tests)
endif()

message(STATUS "uaRO client: CORE_ONLY=${CLIENT_CORE_ONLY}  TESTS=${CLIENT_BUILD_TESTS}")
