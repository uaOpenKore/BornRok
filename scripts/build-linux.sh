#!/usr/bin/env bash
# One command -> Linux x86_64 AND aarch64 (arm64) builds of the BornRok client (static deps).
#
# x86_64 requires: gcc (or clang), CMake 3.24+, vcpkg (VCPKG_ROOT), X11/GL/audio dev headers.
# arm64 is a CROSS build from x86_64 and additionally needs the aarch64 GNU cross-toolchain
#   (Debian/Ubuntu:  sudo apt install g++-aarch64-linux-gnu). It reuses the x86_64-compiled
#   shaders (arch-independent) since an arm64 shaderc can't run on the x64 host.
# See docs/BUILD-linux.md.
#
# Usage:
#   VCPKG_ROOT=/path/to/vcpkg ./scripts/build-linux.sh              # x64 + arm64, gcc
#   VCPKG_ROOT=/path/to/vcpkg ./scripts/build-linux.sh clang        # x64 with clang (+ arm64 gcc-cross)
#   VCPKG_ROOT=/path/to/vcpkg ./scripts/build-linux.sh --no-arm     # x64 only
# Results:
#   build/<x64-preset>/src/BornRok  (+ BornRok-avx2)
#   build/linux-arm/src/BornRok     (arm64)
set -euo pipefail

: "${VCPKG_ROOT:?set VCPKG_ROOT to your vcpkg checkout, e.g. export VCPKG_ROOT=~/vcpkg}"
cd "$(dirname "$0")/.."          # -> Client/
ROOT="$(pwd)"

X64_PRESET="linux-gcc-release"
BUILD_ARM=1
for a in "$@"; do
  case "$a" in
    clang)            X64_PRESET="linux-clang-release" ;;
    x64|--no-arm)     BUILD_ARM=0 ;;
    *) echo "unknown arg: $a (use: clang | --no-arm)"; exit 2 ;;
  esac
done

# 1) x86_64 first — this is where the shaders are compiled.
echo "==> [1/2] x86_64 build ($X64_PRESET, compiles shaders)"
cmake --preset "$X64_PRESET"
cmake --build --preset "$X64_PRESET"
echo "    -> $ROOT/build/$X64_PRESET/src/BornRok"

if [ "$BUILD_ARM" -eq 1 ]; then
  if ! command -v aarch64-linux-gnu-g++ >/dev/null 2>&1; then
    echo "ERROR: aarch64 cross-toolchain not found (aarch64-linux-gnu-g++)."
    echo "       Install it, e.g.:  sudo apt install g++-aarch64-linux-gnu"
    echo "       (x86_64 build above is done; re-run for arm64, or pass --no-arm to skip.)"
    exit 1
  fi
  # 2) arm64 cross build, reusing the shaders compiled in step 1.
  echo "==> [2/2] aarch64 cross build (linux-arm-gcc-release, reuses shaders)"
  cmake --preset linux-arm-gcc-release \
    -DCLIENT_PREBUILT_SHADERS="$ROOT/build/$X64_PRESET/src/shaders"
  cmake --build --preset linux-arm-gcc-release
  echo "    -> $ROOT/build/linux-arm/src/BornRok"
fi

echo "Done."
