#!/bin/bash
# Package the Meridian 59 TUI client for all supported platforms.
# Builds self-contained binaries, scrubs credentials, adds resources + scripts.
# Output: distro/meridian59-tuiclient.zip
#   windows/      — win-x64 exe + launch.bat
#   macos-arm64/  — osx-arm64 binary (Apple Silicon) + launch.sh
#   linux-x64/    — linux-x64 binary + launch.sh
#   linux-aarch64/— linux-arm64 binary (ARM servers/Raspberry Pi etc.) + launch.sh
#   (shared resources at zip root)

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SRC_ROOT="/var/home/mycroft/src/jimsfork/meridian59-dotnet"
BIN_DIR="$SRC_ROOT/bin"
OUT="$REPO_ROOT/distro/meridian59-tuiclient.zip"

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

echo "==> Building self-contained Windows (win-x64)..."
rm -rf "$BUILD_WIN"
dotnet publish "$PROJ" -r win-x64    --self-contained true -c Release \
    -p:PublishSingleFile=true -p:SignAssembly=false -o "$BUILD_WIN" -v quiet
scrub_credentials "$BUILD_WIN"

echo "==> Building self-contained macOS Apple Silicon (osx-arm64)..."
rm -rf "$BUILD_MAC"
dotnet publish "$PROJ" -r osx-arm64  --self-contained true -c Release \
    -p:PublishSingleFile=true -p:SignAssembly=false -o "$BUILD_MAC" -v quiet
scrub_credentials "$BUILD_MAC"

echo "==> Building self-contained Linux x64 (linux-x64)..."
rm -rf "$BUILD_LIN"
dotnet publish "$PROJ" -r linux-x64  --self-contained true -c Release \
    -p:PublishSingleFile=true -p:SignAssembly=false -o "$BUILD_LIN" -v quiet
scrub_credentials "$BUILD_LIN"

echo "==> Building self-contained Linux aarch64 (linux-arm64)..."
rm -rf "$BUILD_ARM"
dotnet publish "$PROJ" -r linux-arm64 --self-contained true -c Release \
    -p:PublishSingleFile=true -p:SignAssembly=false -o "$BUILD_ARM" -v quiet
scrub_credentials "$BUILD_ARM"

echo "==> Assembling zip..."
rm -f "$OUT"
STAGE="/tmp/tuiclient-stage"
rm -rf "$STAGE"
mkdir -p "$STAGE/windows" "$STAGE/macos-arm64" "$STAGE/linux-x64" "$STAGE/linux-aarch64"

# Windows
cp "$BUILD_WIN/Meridian59.TuiClient.exe"        "$STAGE/windows/"
cp "$BUILD_WIN/Meridian59.TuiClient.dll.config" "$STAGE/windows/"
cp "$BUILD_WIN/configuration.xml"               "$STAGE/windows/"
cat > "$STAGE/windows/launch.bat" << 'BAT'
@echo off
Meridian59.TuiClient.exe
BAT

make_launch_sh() {
    cat > "$1/launch.sh" << 'SH'
#!/bin/bash
cd "$(dirname "$0")"
chmod +x Meridian59.TuiClient
./Meridian59.TuiClient
SH
    chmod +x "$1/launch.sh"
}

# macOS Apple Silicon
cp "$BUILD_MAC/Meridian59.TuiClient"            "$STAGE/macos-arm64/"
cp "$BUILD_MAC/Meridian59.TuiClient.dll.config" "$STAGE/macos-arm64/"
cp "$BUILD_MAC/configuration.xml"               "$STAGE/macos-arm64/"
make_launch_sh "$STAGE/macos-arm64"

# Linux x64
cp "$BUILD_LIN/Meridian59.TuiClient"            "$STAGE/linux-x64/"
cp "$BUILD_LIN/Meridian59.TuiClient.dll.config" "$STAGE/linux-x64/"
cp "$BUILD_LIN/configuration.xml"               "$STAGE/linux-x64/"
make_launch_sh "$STAGE/linux-x64"

# Linux aarch64
cp "$BUILD_ARM/Meridian59.TuiClient"            "$STAGE/linux-aarch64/"
cp "$BUILD_ARM/Meridian59.TuiClient.dll.config" "$STAGE/linux-aarch64/"
cp "$BUILD_ARM/configuration.xml"               "$STAGE/linux-aarch64/"
make_launch_sh "$STAGE/linux-aarch64"

# Shared resources (from bin — scripts, recordings, strings/rooms/mails)
cd "$BIN_DIR"
cp -r Resources/strings Resources/rooms Resources/mails "$STAGE/"
for f in *.script vic.json viclr.json vicwalk.json survival.json; do
    [ -e "$f" ] && cp "$f" "$STAGE/" || true
done

# Zip everything
cd "$STAGE"
zip -qr "$OUT" .

SIZE=$(du -sh "$OUT" | cut -f1)
echo "==> Done: $OUT ($SIZE)"
echo ""
echo "To ship: bash $0 --deploy"

deploy() {
    local REMOTE="root@165.22.46.153"
    local REMOTE_ZIP="/opt/m59-account-api/static/meridian59-tuiclient.zip"
    local REMOTE_HTML="/opt/m59-account-api/static/index.html"

    echo "==> Uploading zip..."
    scp "$OUT" "$REMOTE:$REMOTE_ZIP"

    echo "==> Updating filesize in index.html (${SIZE})..."
    ssh "$REMOTE" "sed -i 's/Experimental ASCII Client ([^)]*)/Experimental ASCII Client (${SIZE})/' $REMOTE_HTML"

    echo "==> Shipped (${SIZE})."
}

if [ "${1}" = "--deploy" ]; then
    deploy
fi
