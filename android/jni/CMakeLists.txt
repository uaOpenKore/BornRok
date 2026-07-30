# Android NDK entry point for the uaRO client. Gradle's externalNativeBuild points here; it pulls in
# the normal client CMake (../.. = Client/) which, when ANDROID is set, builds libmain.so (see
# src/CMakeLists.txt: add_library(UaRO SHARED ...) -> OUTPUT_NAME "main"). Tests/AVX2 are off here.
#
# vcpkg supplies the native deps (sdl3, bgfx, harfbuzz, zlib, curl, libvorbis, zstd) for the
# arm64-android triplet -- gradle passes the vcpkg toolchain chain-loaded onto the NDK toolchain (see
# app/build.gradle arguments + docs/android-build-km.md).
cmake_minimum_required(VERSION 3.24)

# NOTE: gradle only re-runs CMake configure when THIS file (or app/build.gradle) changes, not when an
# include()'d .cmake (e.g. CompileShaders.cmake) does -- so edits there need a touch here to take. (S.)

# vcpkg manifest mode looks for vcpkg.json next to CMAKE_SOURCE_DIR (= this android/jni dir), but our
# manifest is Client/vcpkg.json, two levels up. Point vcpkg at it BEFORE project() (the vcpkg toolchain
# loads on the first project() call and reads this then) -- otherwise nothing is installed for the
# arm64-android triplet and find_package(SDL3) fails.
get_filename_component(_client_root "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(VCPKG_MANIFEST_DIR  "${_client_root}" CACHE PATH "" FORCE)
set(VCPKG_MANIFEST_MODE ON               CACHE BOOL "" FORCE)

# Pick the vcpkg android triplet from the ABI. AGP builds each abiFilter in a SEPARATE CMake configure
# and passes -DANDROID_ABI=<abi>, so gradle must NOT hardcode VCPKG_TARGET_TRIPLET -- otherwise both
# ABIs would share one triplet's deps (wrong arch). Map here, BEFORE project() (the vcpkg toolchain
# reads the triplet on the first project() call), so arm64-v8a and x86_64 each get their own deps.
# (S.: x86_64 for Chromebook (Intel/ARCVM) + the LDPlayer emulator.)
if(NOT DEFINED VCPKG_TARGET_TRIPLET OR VCPKG_TARGET_TRIPLET STREQUAL "")
  if(ANDROID_ABI STREQUAL "arm64-v8a")
    set(VCPKG_TARGET_TRIPLET "arm64-android"    CACHE STRING "" FORCE)
  elseif(ANDROID_ABI STREQUAL "x86_64")
    set(VCPKG_TARGET_TRIPLET "x64-android"      CACHE STRING "" FORCE)
  elseif(ANDROID_ABI STREQUAL "armeabi-v7a")
    set(VCPKG_TARGET_TRIPLET "arm-neon-android" CACHE STRING "" FORCE)
  elseif(ANDROID_ABI STREQUAL "x86")
    set(VCPKG_TARGET_TRIPLET "x86-android"      CACHE STRING "" FORCE)
  else()
    message(FATAL_ERROR "uaRO android: unsupported ANDROID_ABI '${ANDROID_ABI}'")
  endif()
  message(STATUS "uaRO android: ABI ${ANDROID_ABI} -> vcpkg triplet ${VCPKG_TARGET_TRIPLET}")
endif()

# vcpkg's own android triplet toolchain (scripts/toolchains/android.cmake) locates the NDK via the
# ANDROID_NDK_HOME env var and errors ("Could not find android ndk") if it's unset -- gradle passes the
# NDK only through VCPKG_CHAINLOAD_TOOLCHAIN_FILE (.../ndk/<ver>/build/cmake/android.toolchain.cmake),
# which vcpkg overrides for its internal compiler-detect. Derive the NDK root from that path and export
# it into the environment (inherited by vcpkg's spawned configure) so no manual env var is needed.
if(DEFINED VCPKG_CHAINLOAD_TOOLCHAIN_FILE AND NOT DEFINED ENV{ANDROID_NDK_HOME})
  get_filename_component(_ndk "${VCPKG_CHAINLOAD_TOOLCHAIN_FILE}" DIRECTORY)  # .../build/cmake
  get_filename_component(_ndk "${_ndk}" DIRECTORY)                            # .../build
  get_filename_component(_ndk "${_ndk}" DIRECTORY)                            # .../ndk/<ver>
  if(EXISTS "${_ndk}")
    set(ENV{ANDROID_NDK_HOME} "${_ndk}")
    message(STATUS "uaRO android: ANDROID_NDK_HOME -> ${_ndk}")
  endif()
endif()

project(uaro_android LANGUAGES CXX)

set(CLIENT_BUILD_TESTS  OFF CACHE BOOL "" FORCE)
set(CLIENT_BUILD_AVX2   OFF CACHE BOOL "" FORCE)
set(CLIENT_CORE_ONLY    OFF CACHE BOOL "" FORCE)

# ../.. resolves to the Client/ root (this file is Client/android/jni/CMakeLists.txt).
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../.." "${CMAKE_CURRENT_BINARY_DIR}/uaro_client")
