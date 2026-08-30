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

ACR122U har en to-farvet LED: rød, grøn og gul (begge tændt).

```bash
./build/nfc games                # auto-scan Steam-bibliotek + non-Steam shortcuts
./build/nfc read                 # vent på tag, vis UID
./build/nfc add bloodrayne       # vent på tag, gem navn+UID i tags.conf
./build/nfc list
./build/nfc remove bloodrayne    # fjern fra listen
./build/nfc remove               # vent på tag, fjern det hvis det er gemt
./build/nfc farver               # rød, grøn, gul + beep
./build/nfc led green
./build/nfc beep 300
./build/nfc firmware
```

`nfc games` finder Steam selv (`~/.steam/steam`, libraryfolders.vdf og `shortcuts.vdf`). Ingen fast spilleliste.

Gemte tags ligger i `tags.conf` i den mappe, du kører fra.
