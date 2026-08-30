# nfc-games

Ny NFC-stack til ACR122U på Bazzite. Første skridt: LED og beep i C++ over USB CCID (ingen `pcscd`, ingen `libnfc`).

## Byg

```bash
cmake -S . -B build
cmake --build build
```

## Hardware

Kernel-driveren `pn533_usb` må ikke eje læseren. Installér udev-reglen én gang:

```bash
sudo cp udev/99-acr122u.rules /etc/udev/rules.d/
sudo udevadm control --reload
sudo udevadm trigger --subsystem-match=usb --attr-match=idVendor=072f --attr-match=idProduct=2200
```

Træk USB-stikket ud og sæt det i igen efter første install.

## Kør

```bash
./build/nfc              # demo: grøn, beep, rød, blink, grøn
./build/nfc led green
./build/nfc led red
./build/nfc led off
./build/nfc beep 300
./build/nfc firmware
```
