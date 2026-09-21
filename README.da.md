# nfc-games

**Download (Linux x86_64):** [release/nfc-games-x86_64.AppImage](release/nfc-games-x86_64.AppImage)  
Gør den kørbar og kør den én gang.

**Brug derhjemme:** [VEJLEDNING.md](VEJLEDNING.md)  
På Gitea: http://192.168.1.3:3002/app/nfc-games/src/branch/main/VEJLEDNING.md

Linux-værktøj: læg et **ACR122U**-NFC-tag på læseren for at starte eller stoppe et **Steam**-spil
(også non-Steam-genveje fra [Steam ROM Manager](https://steamgriddb.github.io/steam-rom-manager/)).

English: [README.md](README.md)

**Testet på:** Bazzite (Fedora) x86_64, ACR122U firmware ACR122U216. Watch kan køre længe (RF-refresh, ingen USB-reset-loop).  
**Ikke:** Windows, macOS eller andre NFC-læsere. Spil kan komme fra Steam,
[Lutris](https://lutris.net) eller et EmuDeck/ES-DE-agtigt `~/Emulation/roms`-træ;
`emu`-typen kan også køre enhver emulator direkte.

## Hvorfor dette findes (sammenlignet med Zaparoo/TapTo)

[Zaparoo](https://zaparoo.org) (efterfølgeren til TapTo) er det
veletablerede valg til at spille fra NFC-tags, men deres ACR122U-driver på
Linux er `libnfc`, hvor **læserens LED og bipper ikke virker, og nogle
clones er inkompatible** (PC/SC-only-clones virker ofte slet ikke på
Linux). nfc-games taler i stedet **direkte med ACR122U via libusb**
(samme CCID-kanal som PC/SC bruger), så LED og buzzer virker normalt,
og clones som den testede `ACR122U216` understøttes.

## Krav

- Linux x86_64
- Steam kørende på samme maskine
- ACR122U USB-læser (`072f:2200`)
- Spil på tags skal allerede ligge i Steam (installeret eller non-Steam-genvej)
- ...eller ligge i Lutris (`nfc lutris`) eller under `~/Emulation/roms` (`nfc emu`)
- ...eller start enhver emulator direkte med `emu`-typen (`nfc add emu '<kommando>'`)
- For at bygge: C++20, CMake ≥ 3.16, pkg-config, libusb-1.0 devel

## Windows 11

En konsol-build til 64-bit Windows 11 laves med `scripts/build-windows.sh`.
Zip-filen ligger i `dist/nfc-games-<version>-windows-x86_64.zip`.

Læseren skal have **WinUSB**-driveren én gang (Zadig, USB `072f:2200`).
Zadig ligger inde i `nfc.exe`. Kør `nfc udev install`. Tags gemmes i
`%APPDATA%\nfc-games\tags.conf`.
Steam skal være installeret og køre; spil startes via `steam://`. Lutris
findes ikke på Windows. Emulator- og handlings-tags kører med `cmd.exe /c`.

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
./build/nfc add action '~/bin/party.sh on'  # bind et tag til en skal-handling (kører via sh -c når tagget lægges på)
./build/nfc start action '~/bin/party.sh on'  # kør en handling nu (intet tag nødvendigt)
./build/nfc add emu 'dolphin -e ~/roms/MarioKartWii.iso'  # bind et tag til et emulator-spil (stoppes på tag-af)
./build/nfc start emu 'dolphin -e ~/roms/MarioKartWii.iso'  # start et emulator-spil nu
./build/nfc lutris         # vis Lutris-spil (wine/native, ikke Steam)
./build/nfc emu            # vis emulator-spil fra ~/Emulation/roms (launcher + args fra SRM)
./build/nfc write          # skriv spilnavn på et allerede bundet tag
./build/nfc watch
./build/nfc udev install
```

LED: **grøn** klar, **gul + 2 bip** tag på, **grøn + 1 bip** tag af, **rød** værktøjet kører ikke.

Træk USB ud og sæt i igen, hvis firmware-læsning fejler første gang.

## AppImage (valgfri)

En samlet binær, hvis du ikke vil beholde kildekoden. Første kørsel
installerer sig selv:

- kopierer altid sig selv til `~/Applications/nfc-games-x86_64.AppImage`
- app-menu (`nfc menu`) plus en systemd-brugerunit, så watch starter ved login
- ACR122U udev-regel (adgangskode via pkexec, én gang)

```bash
./scripts/build-appimage.sh
# output: dist/nfc-games-x86_64.AppImage
# installerer også til ~/Applications og aktiverer + starter watch
```

```bash
chmod +x nfc-games-x86_64.AppImage
./nfc-games-x86_64.AppImage           # installér + menu
./nfc-games-x86_64.AppImage install   # kun installér (lægger også ~/bin/nfc på PATH)
nfc                                   # derefter: alt kører i AppImage
nfc watch
nfc restart
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
| `~/.config/nfc-games/tags.conf` | tag-UID → Steam appid (eller lutris-slug / heroic-app_name / `action` / `emu`-skalkommando) |
| `~/.config/nfc-games/nfc.conf` | sprog (`da`, `en`, `de`, `sv`, `nb`, `fr`) og valgfri `hook_start`/`hook_stop` |

Steam-spil og ROM-genveje bliver i Steam. Programmet tager **ikke** spil eller tags med.

Bind **ikke** tags til `boot-windows` eller Proton/runtime.

## Lutris- og emulator-spil

Begge findes ud fra det, der allerede er installeret; intet kopieres.

- `nfc lutris` viser ikke-Steam-spil fra Lutris (via `lutris -l -o`). Bind ét med
  `nfc add lutris <slug> [navn]`, eller kør `nfc add lutris` uden slug for at vælge
  fra en nummereret liste. Lutris-spil kan startes, men ikke stoppes automatisk.
- `nfc emu` viser emulator-spil under `~/Emulation/roms` og tager launcher og
  argumenter fra samme Steam ROM Manager-config (`userConfigurations.json`), som
  EmuDeck/ES-DE bruger. `nfc add emu` uden argument vælger fra listen; emulatoren
  stoppes automatisk, når tagget løftes. Overstyr ROM-træet med `NFC_ROMS_DIR` og
  SRM-configen med `NFC_SRM_CONFIG`.

## Hooks (valgfrit)

Når `watch` starter eller stopper et Steam-spil, kan den køre en kommando fra `nfc.conf`:

```
hook_start=~/bin/game-on.sh
hook_stop=~/bin/game-off.sh
```

Hver kommando køres løsrevet via `sh -c`, med `$1` = Steam-appid og `$2` = spillets
navn. Config'en læses igen ved hvert udløs, så ændringer gælder uden genstart.
Hooks udløses kun fra `watch`-daemonen — ikke fra `nfc start`/`nfc stop`.

## Licens

[MIT](LICENSE)
