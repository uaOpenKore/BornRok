#!/usr/bin/env bash
# Stage the baked game-data packs into <dest>/content/ for a bundled-platform package
# (Steam/SteamOS, Switch, PS4/PS5, Xbox — platforms where the client ships data instead of patching).
# Shared by every platform's package build so the content pipeline is identical everywhere.
#
# Source of packs (first that exists): $2 arg  ->  $STEAM_DATA_DIR  ->  the repo drop-folder Client/content/.
# The client mounts every *.zip/*.grf (+ optional data/ dir + data.ini) it finds in <install>/content/
# at runtime — see Application.cpp mountGameData. So: drop packs into Client/content/, and any platform
# build stages them here; no patcher needed on the bundled platforms.
#
# Usage:  ./scripts/stage-content.sh <dest-package-root> [data-source-dir]
set -euo pipefail
DEST="${1:?usage: stage-content.sh <dest-package-root> [data-source-dir]}"
cd "$(dirname "$0")/.."          # -> Client/
ROOT="$(pwd)"
SRC="${2:-${STEAM_DATA_DIR:-$ROOT/content}}"

mkdir -p "$DEST/content"
if [ -d "$SRC" ] && find "$SRC" -maxdepth 1 \
      \( -name '*.zip' -o -name '*.grf' -o -name 'data' -o -name 'data.ini' \) 2>/dev/null | grep -q .; then
  # copy the packs (+ optional data/ + data.ini); never the tracked README/.gitkeep
  find "$SRC" -maxdepth 1 -mindepth 1 ! -name 'README.md' ! -name '.gitkeep' \
    -exec cp -a {} "$DEST/content/" \;
  echo "stage-content: baked $(find "$DEST/content" -maxdepth 1 -type f | wc -l) pack(s) from '$SRC' -> '$DEST/content/'"
else
  echo "stage-content: no packs in '$SRC' -> '$DEST/content/' left empty (in-client patcher fetches on first run)"
fi
