# CMake cross-compile toolchain: x86_64 Linux host -> aarch64 (arm64) Linux target.
# Used by the "linux-arm-gcc-release" preset (and chain-loaded by the arm64-linux
# vcpkg overlay triplet) so both the vcpkg deps and the client itself build with the
# aarch64 GNU cross toolchain. Install it on Debian/Ubuntu with:
#   sudo apt install crossbuild-essential-arm64   # gives aarch64-linux-gnu-gcc/g++
# and the arm64 multiarch dev libs (see docs/BUILD-linux.md).
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# Allow overriding the tool prefix (e.g. a different sysroot) via -DCLIENT_ARM64_TOOL_PREFIX=...
if(NOT DEFINED CLIENT_ARM64_TOOL_PREFIX)
  set(CLIENT_ARM64_TOOL_PREFIX aarch64-linux-gnu)
endif()
set(CMAKE_C_COMPILER   ${CLIENT_ARM64_TOOL_PREFIX}-gcc)
set(CMAKE_CXX_COMPILER ${CLIENT_ARM64_TOOL_PREFIX}-g++)

# Programs are HOST tools (compiler, make, shaderc) -> never from the sysroot.
set(CMAKE_FIND_ROOT_PATH /usr/${CLIENT_ARM64_TOOL_PREFIX})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
# Libraries/headers/packages: search BOTH the sysroot AND CMAKE_PREFIX_PATH. This is
# REQUIRED for vcpkg -- it installs the cross deps (miniz, bgfx, ...) under
# vcpkg_installed/arm64-linux (a CMAKE_PREFIX_PATH, NOT under CMAKE_FIND_ROOT_PATH),
# so "ONLY" makes find_package/find_library ignore them and a dep like tinyexr fails
# with "Could not find a package configuration file provided by miniz". (S. arm64-linux log.)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
