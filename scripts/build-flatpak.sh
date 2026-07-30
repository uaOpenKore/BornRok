#!/usr/bin/env bash
# One command -> a Flatpak bundle of the BornRok client (Debian/Ubuntu/Fedora/…).
#
# Bundles the PREBUILT static Linux binary (no rebuild of vcpkg deps inside the sandbox).
# Requires: flatpak + flatpak-builder, and the Freedesktop 24.08 runtime + SDK:
#   flatpak remote-add --if-not-exists --user flathub https://dl.flathub.org/repo/flathub.flatpakrepo
#   flatpak install -y --user flathub org.freedesktop.Platform//24.08 org.freedesktop.Sdk//24.08
# Plus a working VCPKG_ROOT (for the linux build step).
#
# Usage:   VCPKG_ROOT=/path/to/vcpkg ./scripts/build-flatpak.sh            # x86_64 (default)
#          VCPKG_ROOT=/path/to/vcpkg ./scripts/build-flatpak.sh arm64      # aarch64
# Result:  build/BornRok.flatpak         (x86_64)
#          build/BornRok-arm64.flatpak   (aarch64)
#   Install:  flatpak install --user build/BornRok[-arm64].flatpak
#   Run:      flatpak run com.bornrok.BornRok
#
# ARM64 NOTE: the manifest only COPIES the prebuilt binary, but flatpak-builder still runs its
# install-commands INSIDE the aarch64 SDK. On an x86_64 host that needs qemu user emulation + binfmt:
#   sudo apt install -y qemu-user-static binfmt-support   # (Fedora: qemu-user-static)
# The aarch64 runtime/SDK are pulled automatically (--install-deps-from=flathub --arch=aarch64).
# Building ON real arm64 hardware needs no qemu.
set -euo pipefail

# arch: x64 (default) | arm64. Picks which prebuilt binary to stage + which flatpak arch to emit.
ARCH="x64"
for a in "$@"; do
  case "$a" in
    x64|x86_64)    ARCH="x64" ;;
    arm64|aarch64) ARCH="arm64" ;;
    *) echo "unknown arg: $a (use: x64 | arm64)"; exit 2 ;;
  esac
done

: "${VCPKG_ROOT:?set VCPKG_ROOT to your vcpkg checkout, e.g. export VCPKG_ROOT=~/vcpkg}"
command -v flatpak-builder >/dev/null || { echo "ERROR: flatpak-builder not found (sudo dnf/apt install flatpak-builder)"; exit 1; }
cd "$(dirname "$0")/.."          # -> Client/
ROOT="$(pwd)"
FP="$ROOT/packaging/flatpak"

# 1) build the prebuilt Linux binary for the chosen arch.
if [ "$ARCH" = "arm64" ]; then
  FP_ARCH="aarch64"
  OUT="$ROOT/build/BornRok-arm64.flatpak"
  echo "==> [1/3] building linux x86_64 (shaders) + aarch64 cross binary"
  ./scripts/build-linux.sh          # x64 first (compiles the shaders), then the arm64 cross build
  BIN="$ROOT/build/linux-arm/src/BornRok"
  # flatpak-builder runs the aarch64 SDK's install/sh under emulation on an x86_64 host.
  if [ "$(uname -m)" != "aarch64" ] && [ ! -e /proc/sys/fs/binfmt_misc/qemu-aarch64 ]; then
    echo "WARNING: cross-building an aarch64 flatpak on $(uname -m) needs qemu-user-static + binfmt"
    echo "         (sudo apt install -y qemu-user-static binfmt-support). Continuing; flatpak-builder"
    echo "         will fail with an exec-format error if emulation isn't set up."
  fi
else
  FP_ARCH="x86_64"
  OUT="$ROOT/build/BornRok.flatpak"
  echo "==> [1/3] building linux-x64 binary"
  ./scripts/build-linux.sh --no-arm
  BIN="$ROOT/build/linux-gcc-release/src/BornRok"
fi
[ -f "$BIN" ] || { echo "ERROR: binary not found at $BIN"; exit 1; }

# 2) stage the binary + the baked game-data packs next to the manifest (the manifest picks them up).
# stage-content.sh puts Client/content/ (or STEAM_DATA_DIR) into "$FP/content"; the manifest installs
# that to /app/bin/content, which the client mounts at runtime (same pipeline as Steam/MSIX/console).
echo "==> [2/3] staging binary + content"
cp -f "$BIN" "$FP/BornRok"
./scripts/stage-content.sh "$FP"

# 3) build + export a single-file .flatpak bundle (for the chosen arch)
echo "==> [3/3] flatpak-builder + bundle ($FP_ARCH)"
# --disable-rofiles-fuse: the module's `install -Dm755` was failing with
# "install: setting permissions ... Operation not permitted" (S. 2026-07-29). flatpak-builder's
# rofiles-fuse overlay rejects chmod on some filesystems / under root / in containers; disabling it
# makes install/chmod go straight to the build dir. (Slightly less isolation, fine for our copy-only build.)
flatpak-builder --user --force-clean --disable-rofiles-fuse --arch="$FP_ARCH" --install-deps-from=flathub \
  --repo="$ROOT/build/flatpak-repo" "$ROOT/build/flatpak-build" "$FP/com.bornrok.BornRok.yml"
flatpak build-bundle --arch="$FP_ARCH" "$ROOT/build/flatpak-repo" "$OUT" com.bornrok.BornRok
rm -f "$FP/BornRok"              # drop the staged artifacts
rm -rf "$FP/content"

echo "Done: $OUT"
echo "  Install: flatpak install --user $OUT"
echo "  Run:     flatpak run com.bornrok.BornRok"
