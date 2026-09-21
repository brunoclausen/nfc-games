#!/usr/bin/env bash
# Cross-build a Windows 11 x86_64 console binary (nfc.exe) and zip it under dist/.
# On a machine without MinGW, the script rebuilds itself inside Ubuntu via podman or docker.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VERSION="$(tr -d '[:space:]' < "$ROOT/VERSION")"

if ! command -v x86_64-w64-mingw32-g++ >/dev/null 2>&1; then
  ENGINE=""
  for c in podman docker; do
    if command -v "$c" >/dev/null 2>&1; then
      ENGINE="$c"
      break
    fi
  done
  if [[ -z "$ENGINE" ]]; then
    echo "nfc: mingw-w64 and neither podman nor docker were found" >&2
    exit 1
  fi
  exec "$ENGINE" run --rm --network=host -v "$ROOT:/src:Z" -w /src \
    docker.io/library/ubuntu:24.04 bash -c '
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y -qq ca-certificates cmake make g++-mingw-w64-x86-64 \
  pkg-config wget xz-utils zip bzip2 lbzip2 >/dev/null
exec bash /src/scripts/build-windows.sh
'
fi

LIBUSB_VER="${LIBUSB_VER:-1.0.28}"
PREFIX="${LIBUSB_PREFIX:-$ROOT/.cache/libusb-mingw}"
if [[ ! -f "$PREFIX/lib/libusb-1.0.a" && ! -f "$PREFIX/lib/libusb-1.0.dll.a" ]]; then
  tmp="$(mktemp -d)"
  trap 'rm -rf "$tmp"' EXIT
  wget -q -O "$tmp/libusb.tar.bz2" \
    "https://github.com/libusb/libusb/releases/download/v${LIBUSB_VER}/libusb-${LIBUSB_VER}.tar.bz2"
  tar -xf "$tmp/libusb.tar.bz2" -C "$tmp"
  (
    cd "$tmp/libusb-${LIBUSB_VER}"
    ./configure --host=x86_64-w64-mingw32 --prefix="$PREFIX" \
      --enable-static --disable-shared
    make -j"$(nproc)"
    make install
  )
  rm -rf "$tmp"
  trap - EXIT
fi

ZADIG_VER="${ZADIG_VER:-2025.2}"
ZADIG_URL="${ZADIG_URL:-https://github.com/Mr-Precise/SDR-binary-builds-stuff/releases/download/windows/zadig-${ZADIG_VER}-msvc142-Win64.exe}"
zadig_cache="$ROOT/.cache/zadig-${ZADIG_VER}-win64.exe"
if [[ ! -f "$zadig_cache" ]]; then
  mkdir -p "$ROOT/.cache"
  wget -q -O "$zadig_cache.part" "$ZADIG_URL"
  mv "$zadig_cache.part" "$zadig_cache"
fi

rm -rf "$ROOT/build-mingw"
cmake -S "$ROOT" -B "$ROOT/build-mingw" \
  -DCMAKE_TOOLCHAIN_FILE="$ROOT/cmake/mingw-w64-x86_64.cmake" \
  -DCMAKE_FIND_ROOT_PATH="/usr/x86_64-w64-mingw32;${PREFIX}" \
  -DLIBUSB_ROOT="$PREFIX" \
  -DNFC_ZADIG_EXE="$zadig_cache" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$ROOT/build-mingw/install"
cmake --build "$ROOT/build-mingw" --target nfc -j"$(nproc)"
cmake --install "$ROOT/build-mingw"

stage="$ROOT/dist/nfc-games-${VERSION}-windows-x86_64"
rm -rf "$stage"
mkdir -p "$stage/lang"
cp "$ROOT/build-mingw/nfc.exe" "$stage/nfc.exe"
cp "$ROOT/lang/"*.txt "$stage/lang/"
cp "$ROOT/HELP.md" "$ROOT/HELP.en.md" "$ROOT/README.md" "$ROOT/README.da.md" \
  "$ROOT/LICENSE" "$stage/"
cat > "$stage/ZADIG.txt" << EOF
nfc.exe indeholder Zadig ${ZADIG_VER} til 64-bit Windows (GPL-3.0).
Kildekode: https://github.com/pbatard/libwdi
64-bit programfil: https://github.com/Mr-Precise/SDR-binary-builds-stuff
Den officielle Zadig 2.9 fra Akeo er kun 32-bit.
nfc udev install pakker Zadig ud og starter den.
Den installerer WinUSB-driveren til ACR122U (USB 072f:2200).
EOF
cat > "$stage/LEESMIG.txt" << EOF
nfc-games ${VERSION} til Windows 11 (64-bit)

1. Installér WinUSB-driveren én gang. Kør:  nfc.exe udev install
   Zadig 2025.2 til 64-bit ligger inde i nfc.exe (GPL-3.0, se ZADIG.txt).
   Vælg ACR122U / CCID USB Reader, USB 072f:2200, driver WinUSB.
2. Steam skal køre på samme pc.
3. Dobbeltklik nfc.exe for menuen, eller kør i en terminal:
     nfc.exe games
     nfc.exe add
     nfc.exe watch
Tags gemmes i %APPDATA%\\nfc-games\\tags.conf
EOF

# The exe must not depend on MinGW runtime DLLs.
if x86_64-w64-mingw32-objdump -p "$stage/nfc.exe" | grep -E 'DLL Name:.*(libstdc|libgcc|libwinpthread|libusb)'; then
  echo "nfc: windows exe still imports a MinGW or libusb DLL" >&2
  exit 1
fi
zadig_size="$(stat -c%s "$zadig_cache")"
exe_size="$(stat -c%s "$stage/nfc.exe")"
if (( exe_size < zadig_size + 1000000 )); then
  echo "nfc: windows exe does not contain the embedded Zadig installer" >&2
  exit 1
fi

rm -f "$ROOT/dist/nfc-games-${VERSION}-windows-x86_64.zip"
(cd "$ROOT/dist" && zip -qr "nfc-games-${VERSION}-windows-x86_64.zip" "nfc-games-${VERSION}-windows-x86_64")
echo "nfc: $ROOT/dist/nfc-games-${VERSION}-windows-x86_64.zip"
