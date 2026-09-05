#!/usr/bin/env bash
# Build a self-contained nfc-games AppImage (system libusb, no Homebrew at runtime).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build-appimage"
DIST="$ROOT/dist"
TOOLS="$ROOT/.tools"
APPDIR="$BUILD/AppDir"
ARCH="${ARCH:-x86_64}"
VERSION="${VERSION:-}"

if [[ -z "$VERSION" ]]; then
  if [[ -f "$ROOT/VERSION" ]]; then
    VERSION="$(tr -d '[:space:]' < "$ROOT/VERSION")"
  elif git -C "$ROOT" describe --tags --always --dirty >/dev/null 2>&1; then
    VERSION="$(git -C "$ROOT" describe --tags --always --dirty)"
  else
    VERSION="dev"
  fi
fi

LINUXDEPLOY_URL="https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-${ARCH}.AppImage"

mkdir -p "$TOOLS" "$DIST"
chmod +x "$ROOT/packaging/AppRun"

if [[ ! -f "$ROOT/packaging/nfc-games.png" ]]; then
  magick -background none "$ROOT/packaging/nfc-games.svg" -resize 256x256 -depth 8 PNG32:"$ROOT/packaging/nfc-games.png"
fi

if [[ -d /home/linuxbrew/.linuxbrew/lib/pkgconfig ]]; then
  export PKG_CONFIG_PATH="/home/linuxbrew/.linuxbrew/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
fi
cmake -S "$ROOT" -B "$BUILD" \
  -DCMAKE_BUILD_TYPE=Release \
  -DNFC_APPIMAGE=ON
cmake --build "$BUILD" -j"$(nproc)"

rm -rf "$APPDIR"
cmake --install "$BUILD" --prefix "$APPDIR/usr"

install -m 0755 "$ROOT/packaging/AppRun" "$APPDIR/AppRun"
install -m 0644 "$ROOT/packaging/nfc-games.desktop" "$APPDIR/nfc-games.desktop"
install -m 0644 "$ROOT/packaging/nfc-games.png" "$APPDIR/nfc-games.png"
mkdir -p "$APPDIR/usr/lib"

# linuxdeploy blacklists libusb; copy it anyway so the image runs without brew.
# Try multiple locations (ubuntu uses x86_64-linux-gnu, others use lib64)
for libusb_path in /usr/lib64/libusb-1.0.so.0* /usr/lib/x86_64-linux-gnu/libusb-1.0.so.0*; do
  if [[ -e "$libusb_path" ]]; then
    cp -a "$libusb_path" "$APPDIR/usr/lib/" || true
  fi
done

# linuxdeploy expects the icon next to the desktop file in AppDir root as well.
cp -a "$APPDIR/usr/share/icons/hicolor/256x256/apps/nfc-games.png" "$APPDIR/nfc-games.png"

if [[ ! -x "$TOOLS/linuxdeploy-${ARCH}.AppImage" ]]; then
  curl -fL --retry 5 -o "$TOOLS/linuxdeploy-${ARCH}.AppImage" "$LINUXDEPLOY_URL"
  chmod +x "$TOOLS/linuxdeploy-${ARCH}.AppImage"
fi

export APPIMAGE_EXTRACT_AND_RUN=1
export LINUXDEPLOY_OUTPUT_VERSION="$VERSION"
# Write to a temp name so a running AppImage (ETXTBSY) can be replaced via mv.
OUT="$DIST/nfc-games-${ARCH}.AppImage"
TMP_OUT="$DIST/.nfc-games-${ARCH}.AppImage.build"
rm -f "$TMP_OUT"
export LDAI_OUTPUT="$TMP_OUT"
# Keep glibc/libstdc++ on the host; bundle libusb.
export LINUXDEPLOY_OUTPUT_APP_NAME="nfc-games"

cd "$DIST"
"$TOOLS/linuxdeploy-${ARCH}.AppImage" \
  --appdir "$APPDIR" \
  --executable "$APPDIR/usr/bin/nfc" \
  --desktop-file "$APPDIR/nfc-games.desktop" \
  --icon-file "$APPDIR/nfc-games.png" \
  --library /usr/lib64/libusb-1.0.so.0 \
  --library /usr/lib/x86_64-linux-gnu/libusb-1.0.so.0 \
  --custom-apprun "$ROOT/packaging/AppRun" \
  --output appimage

# linuxdeploy names the file from the desktop Name= field; normalize.
if [[ ! -f "$TMP_OUT" ]]; then
  found="$(ls -1 "$DIST"/*-"${ARCH}".AppImage 2>/dev/null | grep -v "\.build$" | head -1 || true)"
  if [[ -n "$found" && "$found" != "$OUT" ]]; then
    mv "$found" "$TMP_OUT"
  fi
fi
if [[ ! -f "$TMP_OUT" ]]; then
  echo "nfc: AppImage was not created" >&2
  exit 1
fi
mv -f "$TMP_OUT" "$OUT"

chmod +x "$OUT"
echo "AppImage: $OUT"
file "$OUT"
ls -lh "$OUT"

if [[ "${NFC_AUTO_INSTALL:-1}" != "0" ]]; then
  echo "Installing AppImage (menu, udev)..."
  "$OUT" install || echo "nfc: auto-install skipped (run the AppImage once)" >&2
  if command -v systemctl >/dev/null 2>&1; then
    systemctl --user disable --now nfc-games.service >/dev/null 2>&1 || true
  fi
fi
