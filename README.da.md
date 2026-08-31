# nfc-games

Linux-værktøj: læg et **ACR122U**-NFC-tag på læseren for at starte eller stoppe et **Steam**-spil
(også non-Steam-genveje fra [Steam ROM Manager](https://steamgriddb.github.io/steam-rom-manager/)).

English: [README.md](README.md)

**Testet på:** Bazzite (Fedora) x86_64, ACR122U firmware ACR122U216.  
**Ikke:** Windows, macOS, andre NFC-læsere, eller spil startet uden om Steam.

## Krav

- Linux x86_64 (ny nok glibc til et Fedora/Bazzite-AppImage)
- Steam kørende på samme maskine
- ACR122U USB-læser (`072f:2200`)
- Spil på tags skal allerede ligge i Steam (installeret eller non-Steam-genvej)

## AppImage

Det er den eneste måde at køre programmet. Brug ikke `./build/nfc`.

Kør AppImage én gang. Den installerer sig selv:

- kopierer altid sig selv til `~/Applications/nfc-games-x86_64.AppImage`
- app-menu (`nfc menu`); `watch` ved login via en user systemd-service
- ACR122U udev-regel (adgangskode via pkexec, én gang)

```bash
chmod +x nfc-games-x86_64.AppImage
./nfc-games-x86_64.AppImage           # installér + watch
./nfc-games-x86_64.AppImage install   # kun installér
```

`./scripts/build-appimage.sh` kører også `install` på denne PC.
Træk USB ud og sæt i igen, hvis firmware-læsning fejler første gang.

```bash
./nfc-games-x86_64.AppImage firmware
./nfc-games-x86_64.AppImage games
./nfc-games-x86_64.AppImage add 123456
./nfc-games-x86_64.AppImage watch
./nfc-games-x86_64.AppImage udev install
```

LED: **grøn** klar, **gul + 2 bip** tag på, **grøn + 1 bip** tag af, **rød** værktøjet kører ikke.

## Config (ikke inde i AppImage)

| Fil | Formål |
| --- | --- |
| `~/.config/nfc-games/tags.conf` | tag-UID → Steam appid |
| `~/.config/nfc-games/nfc.conf` | sprog (`da`, `en`, `de`, `sv`, `nb`, `fr`) |

Steam-spil og ROM-genveje bliver i Steam. AppImage tager **ikke** spil eller tags med.

Bind **ikke** tags til `boot-windows` eller Proton/runtime.

## Byg AppImage

C++20, CMake ≥ 3.16, pkg-config og libusb-1.0 devel.

```bash
sudo dnf install gcc-c++ cmake pkgconf-pkg-config libusb1-devel
./scripts/build-appimage.sh
# output: dist/nfc-games-x86_64.AppImage
# installerer også til ~/Applications og genstarter watch
```

Tests (ikke runtime):

```bash
cmake -S . -B build && cmake --build build && ctest --test-dir build
```

## Licens

[MIT](LICENSE)
