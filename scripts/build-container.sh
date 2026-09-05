#!/usr/bin/env bash
# Build the AppImage inside an Ubuntu 22.04 container so the binary only
# needs glibc 2.34 (runs on Ubuntu 22.04, Debian 12 and newer).
# Falls back to the host build when no container engine is available.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

ENGINE=""
for c in podman docker; do
  if command -v "$c" >/dev/null 2>&1; then
    ENGINE="$c"
    break
  fi
done
if [[ -z "$ENGINE" ]]; then
  echo "nfc: hverken podman eller docker fundet — bruger host-byg" >&2
  NFC_AUTO_INSTALL=0 "$ROOT/scripts/build-appimage.sh"
  exit "$?"
fi

IMAGE="${NFC_BUILD_IMAGE:-docker.io/library/ubuntu:22.04}"
# A host-built CMakeCache in build-appimage points at host paths; drop it.
rm -rf "$ROOT/build-appimage"

exec "$ENGINE" run --rm --network=host -v "$ROOT:/src:Z" -w /src "$IMAGE" bash -c '
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq >/dev/null 2>&1
apt-get install -y -qq cmake g++ make pkg-config libusb-1.0-0-dev file >/dev/null 2>&1
# Ubuntu ships the real libusb in multiarch dir; reproduce the /usr/lib64
# layout used by build-appimage.sh (relative symlink + real file).
mkdir -p /usr/lib64
cp /usr/lib/x86_64-linux-gnu/libusb-1.0.so.0.3.0 /usr/lib64/libusb-1.0.so.0.3.0
ln -sf libusb-1.0.so.0.3.0 /usr/lib64/libusb-1.0.so.0
NFC_AUTO_INSTALL=0 ./scripts/build-appimage.sh
'