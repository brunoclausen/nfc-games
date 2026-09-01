# nfc-games

Linux-værktøj: læg et **ACR122U**-NFC-tag på læseren for at starte eller stoppe et **Steam**-spil
(også non-Steam-genveje fra [Steam ROM Manager](https://steamgriddb.github.io/steam-rom-manager/)).

English: [README.md](README.md)

**Testet på:** Bazzite (Fedora) x86_64, ACR122U firmware ACR122U216.  
**Ikke:** Windows, macOS, andre NFC-læsere, eller spil startet uden om Steam.

## Krav

- Linux x86_64
- Steam kørende på samme maskine
- ACR122U USB-læser (`072f:2200`)
- Spil på tags skal allerede ligge i Steam (installeret eller non-Steam-genvej)
- For at bygge: C++20, CMake ≥ 3.16, pkg-config, libusb-1.0 devel

## Byg og kør (C++)

Det er runtime fra kilden: `./build/nfc`.

```bash
sudo dnf install gcc-c++ cmake pkgconf-pkg-config libusb1-devel
cmake -S . -B build && cmake --build build
ctest --test-dir build
./scripts/full-test.sh --hw   # filer, sprog, CLI, AppImage-build, ACR122U, Steam
```

```bash
./build/nfc firmware
./build/nfc games
./build/nfc add 123456
./build/nfc watch
./build/nfc udev install
```

LED: **grøn** klar, **gul + 2 bip** tag på, **grøn + 1 bip** tag af, **rød** værktøjet kører ikke.

Træk USB ud og sæt i igen, hvis firmware-læsning fejler første gang.

## AppImage (valgfri)

En samlet binær, hvis du ikke vil beholde kildekoden. Første kørsel
installerer sig selv:

- kopierer altid sig selv til `~/Applications/nfc-games-x86_64.AppImage`
- app-menu (`nfc menu`); `watch` ved login via en user systemd-service
- ACR122U udev-regel (adgangskode via pkexec, én gang)

```bash
./scripts/build-appimage.sh
# output: dist/nfc-games-x86_64.AppImage
# installerer også til ~/Applications og genstarter watch
```

```bash
chmod +x nfc-games-x86_64.AppImage
./nfc-games-x86_64.AppImage           # installér + watch
./nfc-games-x86_64.AppImage install   # kun installér
./nfc-games-x86_64.AppImage watch
```

## Læg ændringer på Gitea (gratis)

Efter en ændring, fra denne mappe:

```bash
./scripts/upload.sh --build
```

Det committer, pusher til din Gitea og lægger AppImage på Releasen.
Gitea kører CI (byg + test) selv — uden GitHub-betaling.

- Kode: http://192.168.1.3:3002/app/nfc-games
- Actions: http://192.168.1.3:3002/app/nfc-games/actions
- Release: http://192.168.1.3:3002/app/nfc-games/releases

Når den fulde test på Gitea er grøn, lægges koden og AppImage
automatisk på GitHub (ingen betaling).

Sikkert setup (én gang):

1. Deploy key (kun dette repo, skriveadgang):  
   https://github.com/brunoclausen/nfc-games/settings/keys  
   Allow write access, indsæt `~/.config/nfc-games/ssh/github_nfc_games.pub`
2. Fine-grained token kun til `nfc-games`, Contents: Read and write,  
   så AppImage kan lægges på Releasen:

```bash
./scripts/upload.sh --token ghp_DIN_NØGLE
```

Nøglen ligger kun i `~/.config/nfc-games/github.token` (rettigheder 600)
og som Gitea-secret. Den kommer **ikke** i git.

## Config

| Fil | Formål |
| --- | --- |
| `~/.config/nfc-games/tags.conf` | tag-UID → Steam appid |
| `~/.config/nfc-games/nfc.conf` | sprog (`da`, `en`, `de`, `sv`, `nb`, `fr`) |

Steam-spil og ROM-genveje bliver i Steam. Programmet tager **ikke** spil eller tags med.

Bind **ikke** tags til `boot-windows` eller Proton/runtime.

## Licens

[MIT](LICENSE)
