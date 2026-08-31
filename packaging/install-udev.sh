#!/bin/sh
# Install ACR122U udev rule from the AppImage (must run as root / pkexec).
set -e
RULE="${1:-}"
if [ -z "$RULE" ] || [ ! -f "$RULE" ]; then
  echo "nfc udev: missing rule file" >&2
  exit 1
fi

DEST=/etc/udev/rules.d/99-acr122u.rules
cp "$RULE" "$DEST"
chmod 644 "$DEST"

if command -v udevadm >/dev/null 2>&1; then
  udevadm control --reload
  udevadm trigger --subsystem-match=usb --attr-match=idVendor=072f --attr-match=idProduct=2200 || true
fi

# Kernel NFC must not own the ACR122U.
if [ -d /sys/bus/usb/drivers/pn533_usb ]; then
  for d in /sys/bus/usb/drivers/pn533_usb/*; do
    [ -e "$d" ] || continue
    name="$(basename "$d")"
    case "$name" in
      bind|unbind|uevent|module) continue ;;
    esac
    echo -n "$name" > /sys/bus/usb/drivers/pn533_usb/unbind 2>/dev/null || true
  done
fi

echo "nfc udev: installed $DEST"
