# nx.toolchain.cmake — CMake toolchain for Nintendo Switch (NX, aarch64) via the NintendoSDK clang.
#
# Contains NO proprietary Nintendo material: it references the SDK only BY PATH through the
# NINTENDO_SDK_ROOT environment variable and uses the PUBLIC target triple (aarch64-nintendo-nx-elf,
# visible in any NX binutils name). Safe to commit. Usage (from Client/, in an env that has the SDK):
#
#   set NINTENDO_SDK_ROOT=C:\...\NintendoSDK
#   set UARO_NX_TOOLCHAIN=%CD%\cmake\console-toolchains\nx.toolchain.cmake
#   set UARO_HOST_SHADERC=C:\...\host\shaderc
#   cmake --preset nx
#   cmake --build --preset nx
#
# Target config: NX-NXFP2-a64 (64-bit NX). This file is COMPILATION-focused: the CMake compiler
# check is done as a static library so a full NSO/NRO executable LINK recipe is not required to
# CONFIGURE. The final exe link (nn startup + libnn_* order + SDK linker script) and the
# MakeNso/MakeMeta packaging are wired in the "LINK" section at the bottom — fill those from a
# working SDK sample's build once compilation is green.

if(DEFINED ENV{NINTENDO_SDK_ROOT})
  file(TO_CMAKE_PATH "$ENV{NINTENDO_SDK_ROOT}" NX_SDK_ROOT)
else()
  message(FATAL_ERROR "nx.toolchain: NINTENDO_SDK_ROOT is not set — point it at your NintendoSDK install root.")
endif()

set(CMAKE_SYSTEM_NAME      Generic)     # bare ELF cross; no OS platform module needed
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# --- compilers / archiver ---
# Use the RAW clang under Compilers/NX/nx/aarch64/bin. (The Compilers/NX/bin/nx-clang(++) DRIVER
# WRAPPERS are absent/broken in this repack -- CMake reports "not a full path to an existing compiler
# tool" -- so drive clang directly with the public target triple.)
set(_nx_bin "${NX_SDK_ROOT}/Compilers/NX/nx/aarch64/bin")
set(CMAKE_C_COMPILER   "${_nx_bin}/clang.exe"    CACHE FILEPATH "NX C compiler")
set(CMAKE_CXX_COMPILER "${_nx_bin}/clang++.exe"  CACHE FILEPATH "NX C++ compiler")
set(CMAKE_AR           "${_nx_bin}/llvm-ar.exe"      CACHE FILEPATH "")
set(CMAKE_RANLIB       "${_nx_bin}/llvm-ranlib.exe"  CACHE FILEPATH "")

# Raw clang needs the target triple spelled out (from the binutils prefix aarch64-nintendo-nx-elf-*).
set(CMAKE_C_COMPILER_TARGET   aarch64-nintendo-nx-elf)
set(CMAKE_CXX_COMPILER_TARGET aarch64-nintendo-nx-elf)

# --- headers: public NN API + the NX-NXFP2-a64 target config (nn/TargetConfigs/build_*.h) ---
# If the public headers live somewhere other than <SDK>/Include on this repack, adjust NX_INCLUDE.
set(NX_INCLUDE        "${NX_SDK_ROOT}/Include")
set(NX_TARGET_INCLUDE "${NX_SDK_ROOT}/Common/Configs/Targets/NX-NXFP2-a64/Include")
include_directories(BEFORE SYSTEM "${NX_INCLUDE}" "${NX_TARGET_INCLUDE}")

# --- zlib: the SDK ships it (header in Include, libz.a per target under Libraries/NX-NXFP2-a64) ---
# Point find_package(ZLIB) straight at the SDK's copy so the resource layer (GRF/VFS) links for real.
# Build-config dir: Release (matches the nx preset's CMAKE_BUILD_TYPE=Release; Develop = the debug NX
# config). All the other NX libs (libnn_*) live in the same dir -- that's where the final title link
# will pull them from.
set(NX_LIB_DIR "${NX_SDK_ROOT}/Libraries/NX-NXFP2-a64/Release")
set(ZLIB_INCLUDE_DIR "${NX_INCLUDE}"         CACHE PATH     "NX zlib header (from the SDK)")
set(ZLIB_LIBRARY     "${NX_LIB_DIR}/libz.a"  CACHE FILEPATH "NX zlib static lib (from the SDK)")

# --- bgfx (NVN) : built separately, pointed at via UARO_NX_BGFX_DIR ------------------------------
# The SDK has NO prebuilt bgfx; NX needs bgfx+bx+bimg cross-built for aarch64-nintendo-nx-elf with the
# NVN renderer (bgfx already carries src/nvn/), linking libnvn.a from the NX AddOn (installed via
# TargetManager -- the SDK's Libraries/NX-NXFP2-a64 ships libnn_gfx.a but NOT libnvn.a). Build bgfx
# with THIS toolchain + -DBGFX_CONFIG_RENDERER_NVN=ON + the SDK nvn headers, install it, then
# `set UARO_NX_BGFX_DIR=<bgfx install prefix>` so find_package(bgfx) resolves.
if(DEFINED ENV{UARO_NX_BGFX_DIR})
  file(TO_CMAKE_PATH "$ENV{UARO_NX_BGFX_DIR}" _nx_bgfx)
  list(PREPEND CMAKE_PREFIX_PATH "${_nx_bgfx}")
endif()

# Keep the cross target from picking up host programs/libs.
set(CMAKE_FIND_ROOT_PATH "${NX_SDK_ROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Configure-time compiler probe as a STATIC LIB so CMake does NOT try to LINK a full NX executable
# (which needs the nn startup/specs, wired in the LINK section below).
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# ------------------------------------------------------------------ LINK (fill from an SDK sample) --
# The NX libraries live under ${NX_SDK_ROOT}/Libraries/NX-NXFP2-a64/<Debug|Develop|Release>. A runnable
# title needs: the nn startup object + a specific libnn_* link order + the SDK linker script, then the
# NDK's MakeNso/MakeMeta to package the .nso/.nspd (that packaging is a post-build step, outside CMake).
# Copy the exact link line from a WORKING SDK sample's build log into the two lines below, then flip
# CMAKE_TRY_COMPILE_TARGET_TYPE back to EXECUTABLE to validate a real link.
#   link_directories("${NX_SDK_ROOT}/Libraries/NX-NXFP2-a64/Release")
#   set(CMAKE_EXE_LINKER_FLAGS_INIT "<startup obj + -T linker script + -lnn_... in the SDK's order>")
