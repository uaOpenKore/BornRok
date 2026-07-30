#!/usr/bin/env bash
# One command -> all Linux distribution packages of the BornRok client: Flatpak + SteamOS/Steam.
# Thin orchestrator over build-flatpak.sh and build-steam.sh (each builds the static linux-x64 binary
# it needs). Skips a target gracefully if its tooling is absent so the other still gets produced.
#
# Usage:
#   VCPKG_ROOT=/path/to/vcpkg ./scripts/build-packages.sh                 # both
#   VCPKG_ROOT=... ./scripts/build-packages.sh flatpak                    # just flatpak
#   VCPKG_ROOT=... STEAM_APPID=.. STEAM_DEPOTID=.. ./scripts/build-packages.sh steam
# Results:
#   build/BornRok.flatpak
#   build/steam/ (SteamPipe depot + vdf)  +  build/BornRok-steamos-x86_64.tar.gz
set -euo pipefail

: "${VCPKG_ROOT:?set VCPKG_ROOT to your vcpkg checkout, e.g. export VCPKG_ROOT=~/vcpkg}"
cd "$(dirname "$0")/.."          # -> Client/
DO_FLATPAK=1; DO_STEAM=1
case "${1:-all}" in
  all)     ;;
  flatpak) DO_STEAM=0 ;;
  steam)   DO_FLATPAK=0 ;;
  *) echo "unknown arg: ${1:-} (use: all | flatpak | steam)"; exit 2 ;;
esac

if [ "$DO_FLATPAK" -eq 1 ]; then
  echo "############ Flatpak ############"
  if command -v flatpak-builder >/dev/null 2>&1; then
    ./scripts/build-flatpak.sh
  else
    echo "SKIP flatpak: flatpak-builder not installed (sudo apt/dnf install flatpak-builder)."
  fi
fi

if [ "$DO_STEAM" -eq 1 ]; then
  echo "############ SteamOS / Steam ############"
  ./scripts/build-steam.sh
fi

echo "All requested packages built."
