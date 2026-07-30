#!/usr/bin/env bash
# One command -> a SteamPipe depot for the BornRok client, ready to upload to Steam (Steam Deck /
# SteamOS run it natively via the Steam Linux Runtime — a static x86_64 binary is exactly right).
#
# Bundles the PREBUILT static Linux x86_64 binary. Produces:
#   build/steam/content/        the depot payload (binary + launcher [+ optional data])
#   build/steam/app_build.vdf   } SteamPipe scripts with your appid/depotid + absolute paths filled in
#   build/steam/depot_build.vdf }
#   build/BornRok-steamos-x86_64.tar.gz   a portable tarball (run it as a non-Steam game / no Steamworks acct needed)
#
# Requires: a working VCPKG_ROOT (for the linux build). Uploading additionally needs steamcmd + a
# Steamworks app/depot id.
#
# Usage:
#   VCPKG_ROOT=/path/to/vcpkg STEAM_APPID=2560020 STEAM_DEPOTID=2560021 ./scripts/build-steam.sh
#   # optional: bundle a data folder into the depot (else the client's patcher fetches content on first run)
#   VCPKG_ROOT=... STEAM_APPID=... STEAM_DEPOTID=... STEAM_DATA_DIR=/path/to/gamedata ./scripts/build-steam.sh
# Upload:
#   steamcmd +login <user> +run_app_build "$PWD/build/steam/app_build.vdf" +quit
set -euo pipefail

: "${VCPKG_ROOT:?set VCPKG_ROOT to your vcpkg checkout, e.g. export VCPKG_ROOT=~/vcpkg}"
cd "$(dirname "$0")/.."          # -> Client/
ROOT="$(pwd)"
PKG="$ROOT/packaging/steam"
OUT="$ROOT/build/steam"
CONTENT="$OUT/content"
VERSION="$(cat "$ROOT/VERSION" 2>/dev/null | tr -d '[:space:]' || echo 0.0.0.0)"
APPID="${STEAM_APPID:-0000000}"     # placeholder -> fill from Steamworks
DEPOTID="${STEAM_DEPOTID:-0000001}"

# 1) native static Linux x86_64 build (no arm — Steam Deck is x86_64)
echo "==> [1/4] building linux-x64 binary"
./scripts/build-linux.sh --no-arm
BIN="$ROOT/build/linux-gcc-release/src/BornRok"
[ -f "$BIN" ] || { echo "ERROR: binary not found at $BIN"; exit 1; }

# 2) stage the depot content: the binary + a launcher wrapper (+ optional data)
echo "==> [2/4] staging depot content -> $CONTENT"
rm -rf "$CONTENT"; mkdir -p "$CONTENT"
install -m 0755 "$BIN" "$CONTENT/BornRok"
# Launcher: run from the install dir so relative asset/config lookups resolve on Steam Deck.
cat > "$CONTENT/run.sh" <<'LAUNCH'
#!/usr/bin/env bash
cd "$(dirname "$0")"
exec ./BornRok "$@"
LAUNCH
chmod 0755 "$CONTENT/run.sh"
# Bake the game-data packs into <depot>/content/ via the shared staging helper (same pipeline the
# Switch/PS/Xbox builds use). Source = STEAM_DATA_DIR if set, else the repo drop-folder Client/content/.
./scripts/stage-content.sh "$CONTENT" "${STEAM_DATA_DIR:-}"

# 3) generate the SteamPipe VDFs from the templates (fill appid/depotid/version + absolute paths)
echo "==> [3/4] generating SteamPipe scripts"
mkdir -p "$OUT/output"
subst() {  # $1=template  $2=out
  sed -e "s|@APPID@|$APPID|g" -e "s|@DEPOTID@|$DEPOTID|g" -e "s|@VERSION@|$VERSION|g" \
      -e "s|@CONTENT@|$CONTENT|g" -e "s|@OUTPUT@|$OUT/output|g" -e "s|@DEPOTVDF@|$OUT/depot_build.vdf|g" \
      "$1" > "$2"
}
subst "$PKG/depot_build.vdf.in" "$OUT/depot_build.vdf"
subst "$PKG/app_build.vdf.in"   "$OUT/app_build.vdf"

# 4) portable tarball (works without a Steamworks account: add as a non-Steam game on the Deck)
echo "==> [4/4] portable tarball"
TAR="$ROOT/build/BornRok-steamos-x86_64.tar.gz"
tar -C "$CONTENT" -czf "$TAR" .

echo "Done."
echo "  Depot content : $CONTENT"
echo "  Portable      : $TAR   (extract on the Deck, run ./run.sh)"
if [ "$APPID" = "0000000" ] || [ "$DEPOTID" = "0000001" ]; then
  echo "  NOTE: STEAM_APPID / STEAM_DEPOTID not set -> app_build.vdf has PLACEHOLDER ids."
  echo "        Set them from your Steamworks app before uploading."
fi
echo "  Upload        : steamcmd +login <user> +run_app_build \"$OUT/app_build.vdf\" +quit"
