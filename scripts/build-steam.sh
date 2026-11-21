#!/usr/bin/env bash
# One command -> the SteamPipe depots for the BornRok client, ready to upload to Steam.
#
# The depots carry ONLY the program (S. 2026-11-21): the game data is downloaded by the client's own patcher
# into the per-user folder (%APPDATA%\uaro\client on Windows, ~/.local/share/uaro/client on Linux/Steam Deck),
# never into the Steam install folder. Each depot has the marker file bornrok-userdata.txt beside the exe,
# which makes the client use that per-user folder (see fs::data_dir) and skip its own exe update (Steam updates
# the program).
#
#   Linux / SteamOS / Steam Deck depot: the static x86_64 build (Steam Linux Runtime runs it as is) + run.sh
#   Windows depot (optional): the folder with a Windows BornRok.exe built by scripts/build-windows.ps1
#                             (build\win-msvc\src\Release), given as STEAM_WIN_DIR
# Produces:
#   build/steam/content-linux/, build/steam/content-win/   the depot payloads
#   build/steam/app_build.vdf + depot_build_*.vdf          SteamPipe scripts with your ids + absolute paths
#   build/BornRok-steamos-x86_64.tar.gz                    a portable tarball (non-Steam game on a Deck)
#
# Usage:
#   VCPKG_ROOT=/path/to/vcpkg STEAM_APPID=... STEAM_DEPOTID_LINUX=... \
#     [STEAM_WIN_DIR=/mnt/c/.../build/win-msvc/src/Release STEAM_DEPOTID_WIN=...] ./scripts/build-steam.sh
#   (STEAM_DEPOTID is still accepted for the Linux depot. STEAM_DATA_DIR=/path bakes the data into the
#    depots instead -- not what S. chose, kept for an offline build.)
# Upload:
#   steamcmd +login <user> +run_app_build "$PWD/build/steam/app_build.vdf" +quit
set -euo pipefail

: "${VCPKG_ROOT:?set VCPKG_ROOT to your vcpkg checkout, e.g. export VCPKG_ROOT=~/vcpkg}"
cd "$(dirname "$0")/.."          # -> Client/
ROOT="$(pwd)"
PKG="$ROOT/packaging/steam"
OUT="$ROOT/build/steam"
VERSION="$(cat "$ROOT/VERSION" 2>/dev/null | tr -d '[:space:]' || echo 0.0.0.0)"
APPID="${STEAM_APPID:-0000000}"     # placeholders -> fill from Steamworks
DEPOT_LINUX="${STEAM_DEPOTID_LINUX:-${STEAM_DEPOTID:-0000001}}"
DEPOT_WIN="${STEAM_DEPOTID_WIN:-0000002}"
WIN_DIR="${STEAM_WIN_DIR:-}"
mkdir -p "$OUT/output"

marker() {  # $1 = depot content dir
  cat > "$1/bornrok-userdata.txt" <<'TXT'
BornRok (Steam): this file tells the client to keep its game data, settings and logs in the per-user folder
(Windows: %APPDATA%\uaro\client, Linux/Steam Deck: ~/.local/share/uaro/client), not in this folder.
Do not delete it.
TXT
}

# 1) Linux depot: the native static x86_64 build (no arm -- Steam Deck is x86_64)
echo "==> [1/4] building linux-x64 binary"
./scripts/build-linux.sh --no-arm
BIN="$ROOT/build/linux-gcc-release/src/BornRok"
[ -f "$BIN" ] || { echo "ERROR: binary not found at $BIN"; exit 1; }
LCONTENT="$OUT/content-linux"
echo "==> [2/4] staging the depots"
rm -rf "$LCONTENT" "$OUT/content"; mkdir -p "$LCONTENT"
install -m 0755 "$BIN" "$LCONTENT/BornRok"
# Launcher: run from the install dir so relative asset lookups resolve on Steam Deck.
cat > "$LCONTENT/run.sh" <<'LAUNCH'
#!/usr/bin/env bash
cd "$(dirname "$0")"
exec ./BornRok "$@"
LAUNCH
chmod 0755 "$LCONTENT/run.sh"
marker "$LCONTENT"
if [ -n "${STEAM_DATA_DIR:-}" ]; then ./scripts/stage-content.sh "$LCONTENT" "$STEAM_DATA_DIR"; fi

# 2) Windows depot (when a Windows build folder is given)
WCONTENT="$OUT/content-win"
rm -rf "$WCONTENT"
if [ -n "$WIN_DIR" ]; then
  [ -f "$WIN_DIR/BornRok.exe" ] || { echo "ERROR: no BornRok.exe in STEAM_WIN_DIR=$WIN_DIR"; exit 1; }
  mkdir -p "$WCONTENT"
  # the exe and the DLLs/assets beside it; no debug symbols, no MSIX
  find "$WIN_DIR" -maxdepth 1 -type f ! -name '*.pdb' ! -name '*.ilk' ! -name '*.exp' ! -name '*.lib' \
       ! -name '*.msix' -exec cp -p {} "$WCONTENT/" \;
  for d in shaders fonts texts; do if [ -d "$WIN_DIR/$d" ]; then cp -rp "$WIN_DIR/$d" "$WCONTENT/"; fi; done
  marker "$WCONTENT"
  if [ -n "${STEAM_DATA_DIR:-}" ]; then ./scripts/stage-content.sh "$WCONTENT" "$STEAM_DATA_DIR"; fi
else
  echo "    (no STEAM_WIN_DIR: Linux depot only)"
fi

# 3) the SteamPipe VDFs from the templates (ids, version, absolute paths)
echo "==> [3/4] generating SteamPipe scripts"
depot_vdf() {  # $1=depot id  $2=content dir  $3=out
  sed -e "s|@DEPOTID@|$1|g" -e "s|@CONTENT@|$2|g" "$PKG/depot_build.vdf.in" > "$3"
}
depot_vdf "$DEPOT_LINUX" "$LCONTENT" "$OUT/depot_build_linux.vdf"
DEPOTS="		\"$DEPOT_LINUX\"	\"$OUT/depot_build_linux.vdf\""
if [ -n "$WIN_DIR" ]; then
  depot_vdf "$DEPOT_WIN" "$WCONTENT" "$OUT/depot_build_win.vdf"
  DEPOTS="$DEPOTS
		\"$DEPOT_WIN\"	\"$OUT/depot_build_win.vdf\""
fi
awk -v appid="$APPID" -v ver="$VERSION" -v out="$OUT/output" -v content="$OUT" -v depots="$DEPOTS" '
  { gsub(/@APPID@/, appid); gsub(/@VERSION@/, ver); gsub(/@OUTPUT@/, out); gsub(/@CONTENT@/, content) }
  /@DEPOTS@/ { print depots; next }
  { print }' "$PKG/app_build.vdf.in" > "$OUT/app_build.vdf"
rm -f "$OUT/depot_build.vdf"

# 4) portable tarball (works without a Steamworks account: add as a non-Steam game on the Deck)
echo "==> [4/4] portable tarball"
TAR="$ROOT/build/BornRok-steamos-x86_64.tar.gz"
tar -C "$LCONTENT" -czf "$TAR" .

echo "Done."
echo "  Linux depot   : $LCONTENT  (depot $DEPOT_LINUX)"
if [ -n "$WIN_DIR" ]; then echo "  Windows depot : $WCONTENT  (depot $DEPOT_WIN)"; fi
echo "  Portable      : $TAR   (extract on the Deck, run ./run.sh)"
if [ "$APPID" = "0000000" ] || [ "$DEPOT_LINUX" = "0000001" ] || { [ -n "$WIN_DIR" ] && [ "$DEPOT_WIN" = "0000002" ]; }; then
  echo "  NOTE: STEAM_APPID / STEAM_DEPOTID_* not set -> app_build.vdf has PLACEHOLDER ids."
  echo "        Set them from your Steamworks app before uploading."
fi
echo "  Upload        : steamcmd +login <user> +run_app_build \"$OUT/app_build.vdf\" +quit"
