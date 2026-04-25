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
    cd "$BIN_DIR"
    cp -r Resources/strings Resources/rooms Resources/mails "$dest/"
    for f in *.script vic.json viclr.json vicwalk.json survival.json; do
        [ -e "$f" ] && cp "$f" "$dest/" || true
    done
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

    echo "==> Uploading zips..."
    scp "$OUT_WIN" "$OUT_MAC" "$OUT_LIN" "$OUT_ARM" "$REMOTE:$STATIC/"

    echo "==> Updating filesizes in index.html..."
    ssh "$REMOTE" "python3 - << 'PYEOF'
import re
with open('$HTML', 'r') as f:
    html = f.read()
# Update data-size-* attributes
html = re.sub(r'data-size-windows=\"[^\"]*\"',      'data-size-windows=\"$SIZE_WIN\"',      html)
html = re.sub(r'data-size-macos=\"[^\"]*\"',        'data-size-macos=\"$SIZE_MAC\"',        html)
html = re.sub(r'data-size-linux-x64=\"[^\"]*\"',    'data-size-linux-x64=\"$SIZE_LIN\"',    html)
html = re.sub(r'data-size-linux-aarch64=\"[^\"]*\"','data-size-linux-aarch64=\"$SIZE_ARM\"',html)
# Update visible label spans
html = re.sub(r'(<span class=\"opacity-50\" data-label-windows>)[^<]*(</span>)',      r'\g<1>$SIZE_WIN\g<2>',  html)
html = re.sub(r'(<span class=\"opacity-50\" data-label-macos>)[^<]*(</span>)',        r'\g<1>$SIZE_MAC\g<2>',  html)
html = re.sub(r'(<span class=\"opacity-50\" data-label-linux-x64>)[^<]*(</span>)',    r'\g<1>$SIZE_LIN\g<2>',  html)
html = re.sub(r'(<span class=\"opacity-50\" data-label-linux-aarch64>)[^<]*(</span>)',r'\g<1>$SIZE_ARM\g<2>',  html)
with open('$HTML', 'w') as f:
    f.write(html)
print('Sizes patched.')
PYEOF
"

    echo "==> Shipped."
    echo "    windows:       $SIZE_WIN"
    echo "    macos-arm64:   $SIZE_MAC"
    echo "    linux-x64:     $SIZE_LIN"
    echo "    linux-aarch64: $SIZE_ARM"
}

if [ "${1}" = "--deploy" ]; then
    deploy
fi
