# nfc-games hjælp

C++-program til ACR122U: LED, beep, NFC-tags og Steam-spil.

Program: `./build/nfc`  
Kør kommandoerne fra `~/nfc-games`.

```bash
cd ~/nfc-games
./build/nfc --help
./build/nfc help
```

## Kommandoer

| Kommando | Hvad den gør |
| --- | --- |
| `nfc watch` | Tag på = gul + 1 beep + start spil; tag af = stop + grøn |
| `nfc games` | Auto-scan alle Steam-spil og non-Steam shortcuts |
| `nfc start "BloodRayne 2"` | Start spil via Steam (kun ét ad gangen) |
| `nfc start` | Læs tag og start det bundne spil |
| `nfc lock` | Vis hvilket spil der kører/er låst |
| `nfc stop` | Stop det kørende spil |
| `nfc add "Silent Hill 4"` | Find spillet, vent på tag, bind tag → spil |
| `nfc add 3640596865` | Samme, men med appid (når flere har samme navn) |
| `nfc read` | Læs tag, vis UID og bundet spil |
| `nfc list` | Vis gemte tags |
| `nfc remove "Silent Hill 4"` | Fjern tag fra listen |
| `nfc remove` | Læs tag og fjern det, hvis det er gemt |
| `nfc farver` | Test rød, grøn, gul LED + beep |
| `nfc led green\|red\|yellow\|off` | Sæt LED |
| `nfc beep 300` | Bip (ms) |
| `nfc firmware` | Vis ACR122U-firmware |
| `nfc --help` / `nfc -h` | Kort hjælp i terminalen |
| `nfc help` | Denne hjælp (HELP.md / HELP.en.md) |
| `nfc sprog` / `nfc lang` | Vis sprog og hvilket der er aktivt |
| `nfc sprog en` / `nfc lang en` | Skift sprog (da, en) og gem det |
| `nfc udev` | Vis udev-regel til ACR122U |
| `nfc udev install` | Installér udev-regel (pkexec, ligger i AppImage) |

## Tag-liste

Gemte tags ligger i:

`~/.config/nfc-games/tags.conf`

(`$NFC_TAGS` overstyrer. Første kørsel kopierer den gamle `~/nfc-games/tags.conf`, hvis den findes.)

Format:

```
# uid  kind  appid  name
04AABBCCDD  steam  1373550  BloodRayne 2: Terminal Cut
```

Filen oprettes først, når et tag bliver gemt med `nfc add`.

## Sprog

Tekster i `nfc` kan skiftes:

```bash
./build/nfc sprog          # liste (aktivt sprog er markeret)
./build/nfc sprog en       # English
./build/nfc sprog da       # Dansk
```

`nfc lang` er det samme. Valget gemmes i `~/.config/nfc-games/nfc.conf`.  
`$NFC_LANG` (fx `en` eller `da`) overstyrer filen for én kørsel.

## Steam-liste

Ingen fast spillefil. `nfc games` scanner selv hver gang:

- Steam-rod (`~/.steam/steam`, `~/.local/share/Steam`)
- `libraryfolders.vdf` + `appmanifest_*.acf` (installerede Steam-spil)
- `shortcuts.vdf` — emu-spil fra **Steam ROM Manager**
  (https://steamgriddb.github.io/steam-rom-manager/)

SRM skriver genveje ind i Steam (PCSX2, Ryujinx, ES-DE, …). NFC læser de samme
genveje og starter dem kun via Steam, f.eks.:

```
steam steam://rungameid/15636244473128681472
```

(det 64-bit id SRM/Steam putter i `.desktop`-filen). NFC kører ikke emulatoren
direkte. Parse nye roms i SRM, genstart Steam, så ligger de i `nfc games`.

Proton og Steam Runtime vises ikke.

Alle spil **og** emu-genveje startes kun via Steam (`steam steam://rungameid/...`).

Har to spil samme navn (fx Batman), så brug **appid** med `nfc add`.

## Hardware

Læser: ACR122U (USB `072f:2200`). LED er to-farvet: rød, grøn, gul (begge).

Første gang, én udev-regel så kernel-NFC ikke stjæler læseren:

```bash
sudo cp udev/99-acr122u.rules /etc/udev/rules.d/
sudo udevadm control --reload
```

Træk USB ud og sæt i igen. Grøn LED = klar. Gul + beep = tag fundet.

## Byg

```bash
cd ~/nfc-games
cmake -S . -B build
cmake --build build
```

## AppImage

Fuld, selvstændig binær (system-libusb, hjælp, udev-regel, ikon):

```bash
cd ~/nfc-games
./scripts/build-appimage.sh
./dist/nfc-games-x86_64.AppImage --help
./dist/nfc-games-x86_64.AppImage games
./dist/nfc-games-x86_64.AppImage watch
```

Tags gemmes i `~/.config/nfc-games/tags.conf` (kopieres fra `~/nfc-games/tags.conf` første gang, hvis den findes). Dobbeltklik i filhåndtering starter `nfc watch` i en terminal.

USB-læseren: AppImage indeholder udev-regel og installer. Første `watch`/`add`/`read` spørger om root via pkexec, hvis reglen mangler. Manuelt: `./dist/nfc-games-x86_64.AppImage udev install`.

## Typisk brug

```bash
cd ~/nfc-games
./build/nfc games
./build/nfc add "BloodRayne 2"
./build/nfc watch
```

`nfc watch` kører indtil Ctrl+C.

- **Grøn** — scanneren er klar og lytter
- **Gul + ét beep** — tag på, spil startes
- **Rød** — scanneren lytter ikke (watch er stoppet, eller USB-fejl)

Tag af: spil stoppes, LED bliver grøn igen (lytter). Ctrl+C: LED rød.
