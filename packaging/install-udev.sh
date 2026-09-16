#!/bin/sh
# Install ACR122U udev rule + blacklist kernel NFC (must run as root / pkexec).
set -e
RULE="${1:-}"
if [ -z "$RULE" ] || [ ! -f "$RULE" ]; then
  echo "nfc udev: missing rule file" >&2
  exit 1
fi

DEST=/etc/udev/rules.d/99-acr122u.rules
cp "$RULE" "$DEST"
chmod 644 "$DEST"

# Kernel NFC must not own the ACR122U. Blacklist survives reboot; unbind is immediate.
mkdir -p /etc/modprobe.d
cat > /etc/modprobe.d/nfc-games-acr122u.conf << 'EOF'
# nfc-games talks to the ACR122U in userspace (libusb).
blacklist pn533_usb
blacklist pn533
EOF
chmod 644 /etc/modprobe.d/nfc-games-acr122u.conf

if command -v udevadm >/dev/null 2>&1; then
  udevadm control --reload
  udevadm trigger --subsystem-match=usb --attr-match=idVendor=072f --attr-match=idProduct=2200 || true
fi

unbind_driver() {
  drv="$1"
  [ -d "/sys/bus/usb/drivers/$drv" ] || return 0
  for d in "/sys/bus/usb/drivers/$drv"/*; do
    [ -e "$d" ] || continue
    name="$(basename "$d")"
    case "$name" in
      bind|unbind|uevent|module|new_id|remove_id) continue ;;
    esac
    echo -n "$name" > "/sys/bus/usb/drivers/$drv/unbind" 2>/dev/null || true
  done
}

unbind_driver pn533_usb
unbind_driver pn533

modprobe -r pn533_usb 2>/dev/null || true
modprobe -r pn533 2>/dev/null || true

echo "nfc udev: installed $DEST"
echo "nfc udev: blacklisted pn533_usb in /etc/modprobe.d/nfc-games-acr122u.conf"
