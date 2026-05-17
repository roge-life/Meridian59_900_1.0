#!/bin/bash
# Package the Meridian 59 TUI client for all supported platforms.
# Builds self-contained binaries, scrubs credentials, adds resources + scripts.
# Output: distro/meridian59-tuiclient-{platform}.zip x4
#   meridian59-tuiclient-windows.zip
#   meridian59-tuiclient-macos-arm64.zip
#   meridian59-tuiclient-linux-x64.zip
#   meridian59-tuiclient-linux-aarch64.zip

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SRC_ROOT="/var/home/mycroft/src/jimsfork/meridian59-dotnet"
BIN_DIR="$SRC_ROOT/bin"
DISTRO="$REPO_ROOT/distro"

BUILD_WIN="/tmp/tuiclient-win-x64"
BUILD_MAC="/tmp/tuiclient-osx-arm64"
BUILD_LIN="/tmp/tuiclient-linux-x64"
BUILD_ARM="/tmp/tuiclient-linux-arm64"

PROJ="$SRC_ROOT/Meridian59.TuiClient/Meridian59.TuiClient.csproj"

scrub_credentials() {
    sed -i \
        -e 's/username="[^"]*"/username=""/' \
        -e 's/password="[^"]*"/password=""/' \
        -e 's/character="[^"]*"/character=""/' \
        "$1/configuration.xml"
}

publish_platform() {
    local rid="$1" out="$2"
    rm -rf "$out"
    # Override OutputPath so the csproj's ../bin is never touched during publish
    dotnet publish "$PROJ" -r "$rid" --self-contained true -c Release \
        -p:PublishSingleFile=true -p:SignAssembly=false \
        -p:OutputPath="$out/obj" \
        -o "$out" -v quiet
    scrub_credentials "$out"
}

echo "==> Building self-contained Windows (win-x64)..."
publish_platform win-x64    "$BUILD_WIN"

echo "==> Building self-contained macOS Apple Silicon (osx-arm64)..."
publish_platform osx-arm64  "$BUILD_MAC"

echo "==> Building self-contained Linux x64 (linux-x64)..."
publish_platform linux-x64  "$BUILD_LIN"

echo "==> Building self-contained Linux aarch64 (linux-arm64)..."
publish_platform linux-arm64 "$BUILD_ARM"

make_launch_sh() {
    cat > "$1/launch.sh" << 'SH'
#!/bin/bash
cd "$(dirname "$0")"
chmod +x Meridian59.TuiClient
./Meridian59.TuiClient
SH
    chmod +x "$1/launch.sh"
}

copy_resources() {
    local dest="$1"
    
    mkdir -p "$dest/Resources"
    # Only copy TUI-relevant resources, not the heavy 3D assets/textures
    for folder in ui sounds-crushed music-crushed; do
        [ -d "$SRC_ROOT/Resources/$folder" ] && cp -r "$SRC_ROOT/Resources/$folder" "$dest/Resources/"
    done
    
    cd "$BIN_DIR"
    # strings, rooms, mails live at bin/ level for testing but we put them in root for distro
    [ -d "strings" ] && cp -r strings "$dest/"
    [ -d "rooms" ]   && cp -r rooms "$dest/"
    [ -d "mails" ]   && cp -r mails "$dest/"
    for f in *.script vic.json viclr.json vicwalk.json survival.json; do
        [ -e "$f" ] && cp "$f" "$dest/" || true
    done
}

copy_bass() {
    local dest="$1" platform="$2"
    local bass_src="$SRC_ROOT/Meridian59.TuiClient"
    case "$platform" in
        win*)    [ -f "$bass_src/bass.dll" ]       && cp "$bass_src/bass.dll"       "$dest/" ;;
        osx*)    [ -f "$bass_src/libbass.dylib" ]  && cp "$bass_src/libbass.dylib"  "$dest/" ;;
        linux*)  [ -f "$bass_src/libbass.so" ]     && cp "$bass_src/libbass.so"     "$dest/" ;;
    esac
}


echo "==> Assembling per-platform zips..."
mkdir -p "$DISTRO"

# Windows
STAGE_WIN="/tmp/tuiclient-stage-windows"
rm -rf "$STAGE_WIN" && mkdir -p "$STAGE_WIN"
cp "$BUILD_WIN/Meridian59.TuiClient.exe"        "$STAGE_WIN/"
cp "$BUILD_WIN/Meridian59.TuiClient.dll.config" "$STAGE_WIN/"
cp "$BUILD_WIN/configuration.xml"               "$STAGE_WIN/"
cat > "$STAGE_WIN/launch.bat" << 'BAT'
@echo off
Meridian59.TuiClient.exe
BAT
copy_resources "$STAGE_WIN"
copy_bass "$STAGE_WIN" win
OUT_WIN="$DISTRO/meridian59-tuiclient-windows.zip"
rm -f "$OUT_WIN" && cd "$STAGE_WIN" && zip -qr "$OUT_WIN" .
SIZE_WIN=$(du -sh "$OUT_WIN" | cut -f1)
echo "    windows:       $OUT_WIN ($SIZE_WIN)"

# macOS Apple Silicon
STAGE_MAC="/tmp/tuiclient-stage-macos-arm64"
rm -rf "$STAGE_MAC" && mkdir -p "$STAGE_MAC"
cp "$BUILD_MAC/Meridian59.TuiClient"            "$STAGE_MAC/"
cp "$BUILD_MAC/Meridian59.TuiClient.dll.config" "$STAGE_MAC/"
cp "$BUILD_MAC/configuration.xml"               "$STAGE_MAC/"
make_launch_sh "$STAGE_MAC"
copy_resources "$STAGE_MAC"
copy_bass "$STAGE_MAC" osx
OUT_MAC="$DISTRO/meridian59-tuiclient-macos-arm64.zip"
rm -f "$OUT_MAC" && cd "$STAGE_MAC" && zip -qr "$OUT_MAC" .
SIZE_MAC=$(du -sh "$OUT_MAC" | cut -f1)
echo "    macos-arm64:   $OUT_MAC ($SIZE_MAC)"

# Linux x64
STAGE_LIN="/tmp/tuiclient-stage-linux-x64"
rm -rf "$STAGE_LIN" && mkdir -p "$STAGE_LIN"
cp "$BUILD_LIN/Meridian59.TuiClient"            "$STAGE_LIN/"
cp "$BUILD_LIN/Meridian59.TuiClient.dll.config" "$STAGE_LIN/"
cp "$BUILD_LIN/configuration.xml"               "$STAGE_LIN/"
make_launch_sh "$STAGE_LIN"
copy_resources "$STAGE_LIN"
copy_bass "$STAGE_LIN" linux
OUT_LIN="$DISTRO/meridian59-tuiclient-linux-x64.zip"
rm -f "$OUT_LIN" && cd "$STAGE_LIN" && zip -qr "$OUT_LIN" .
SIZE_LIN=$(du -sh "$OUT_LIN" | cut -f1)
echo "    linux-x64:     $OUT_LIN ($SIZE_LIN)"

# Linux aarch64
STAGE_ARM="/tmp/tuiclient-stage-linux-aarch64"
rm -rf "$STAGE_ARM" && mkdir -p "$STAGE_ARM"
cp "$BUILD_ARM/Meridian59.TuiClient"            "$STAGE_ARM/"
cp "$BUILD_ARM/Meridian59.TuiClient.dll.config" "$STAGE_ARM/"
cp "$BUILD_ARM/configuration.xml"               "$STAGE_ARM/"
make_launch_sh "$STAGE_ARM"
copy_resources "$STAGE_ARM"
copy_bass "$STAGE_ARM" linux
OUT_ARM="$DISTRO/meridian59-tuiclient-linux-aarch64.zip"
rm -f "$OUT_ARM" && cd "$STAGE_ARM" && zip -qr "$OUT_ARM" .
SIZE_ARM=$(du -sh "$OUT_ARM" | cut -f1)
echo "    linux-aarch64: $OUT_ARM ($SIZE_ARM)"

echo ""
echo "==> Done. Run with --deploy to ship."

deploy() {
    local REMOTE="root@165.22.46.153"
    local STATIC="/opt/m59-account-api/static"
    local HTML="$STATIC/index.html"

    echo "==> Uploading zips and patch script..."
    scp "$OUT_WIN" "$OUT_MAC" "$OUT_LIN" "$OUT_ARM" "$REMOTE:$STATIC/"
    scp "$SCRIPT_DIR/../webapi/patch_sizes.py" "$REMOTE:/opt/m59-account-api/patch_sizes.py"

    echo "==> Updating filesizes in index.html..."
    ssh "$REMOTE" "python3 /opt/m59-account-api/patch_sizes.py $SIZE_WIN $SIZE_MAC $SIZE_LIN $SIZE_ARM" 
    echo "==> Shipped."
    echo "    windows:       $SIZE_WIN"
    echo "    macos-arm64:   $SIZE_MAC"
    echo "    linux-x64:     $SIZE_LIN"
    echo "    linux-aarch64: $SIZE_ARM"
}

if [ "${1}" = "--deploy" ]; then
    deploy
fi
