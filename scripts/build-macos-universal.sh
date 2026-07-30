#!/usr/bin/env bash
# One command -> a UNIVERSAL (arm64 + x86_64) macOS build of the BornRok client.
#
# Why a script and not a single `cmake --build`: vcpkg builds dependencies per-arch
# (arm64-osx OR x64-osx), so a lone `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"` fails to link.
# We build each arch (reusing the host-compiled shaders, which are arch-independent) and
# `lipo` them into one fat binary.
#
# Usage:
#   VCPKG_ROOT=/path/to/vcpkg ./scripts/build-macos-universal.sh
# Result:
#   Client/build/BornRok   (run `lipo -info` on it -> "arm64 x86_64")
set -euo pipefail

: "${VCPKG_ROOT:?set VCPKG_ROOT to your vcpkg checkout, e.g. export VCPKG_ROOT=~/vcpkg}"
cd "$(dirname "$0")/.."          # -> Client/
ROOT="$(pwd)"

# 1) Native-arch build first — this is where bgfx's shaderc compiles the shaders.
echo "==> [1/3] native-arch build (compiles shaders)"
cmake --preset mac-clang-release
cmake --build --preset mac-clang-release
SHADERS="$ROOT/build/mac-clang-release/src/shaders"
HOST_BIN="$ROOT/build/mac-clang-release/src/BornRok"

# 2) The OTHER arch, reusing the shaders from step 1 (shader binaries don't depend on CPU arch).
HOST_ARCH="$(uname -m)"          # arm64 (Apple Silicon) or x86_64 (Intel)
if [ "$HOST_ARCH" = "arm64" ]; then
  OTHER_ARCH="x86_64"; OTHER_TRIPLET="x64-osx"
else
  OTHER_ARCH="arm64";  OTHER_TRIPLET="arm64-osx"
fi
echo "==> [2/3] $OTHER_ARCH build (reuses shaders)"
cmake --preset mac-clang-release -B "build/mac-$OTHER_ARCH" \
  -DCMAKE_OSX_ARCHITECTURES="$OTHER_ARCH" \
  -DVCPKG_TARGET_TRIPLET="$OTHER_TRIPLET" \
  -DCLIENT_PREBUILT_SHADERS="$SHADERS"
cmake --build "build/mac-$OTHER_ARCH"
OTHER_BIN="$ROOT/build/mac-$OTHER_ARCH/src/BornRok"

# 3) Fuse the two single-arch binaries into one universal binary.
echo "==> [3/3] lipo -> build/BornRok (universal)"
lipo -create -output "$ROOT/build/BornRok" "$HOST_BIN" "$OTHER_BIN"
lipo -info "$ROOT/build/BornRok"
echo "Done: $ROOT/build/BornRok"
