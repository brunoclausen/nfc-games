# nfc-games hjælp

C++-program til ACR122U: LED, beep, NFC-tags og Steam-spil.

Kør den byggede binær `./build/nfc`. AppImage er valgfri.

```bash
cmake -S . -B build && cmake --build build
./build/nfc --help
./build/nfc help
./build/nfc watch
```

## Kommandoer

| Kommando | Hvad den gør |
| --- | --- |
| `nfc menu` | Interaktiv menu (tal-valg) |
| `nfc watch` | Tag på = gul + 2 beep + start spil; tag af = 1 beep + stop + grøn |
| `nfc restart` / `nfc genstart` | Genstart watch (systemd-tjeneste, ellers pid) |
| `nfc games` | Auto-scan alle Steam-spil og non-Steam shortcuts |
| `nfc start "BloodRayne 2"` | Start spil via Steam (kun ét ad gangen) |
| `nfc start` | Læs tag og start det bundne spil |
| `nfc lock` | Vis hvilket spil der kører/er låst |
| `nfc stop` | Stop det kørende spil |
| `nfc add "Silent Hill 4"` | Find spillet, vent på tag, bind tag → spil og skriv navn (NDEF) |
| `nfc add 3640596865` | Samme, men med appid (når flere har samme navn) |
| `nfc write` | Skriv det bundne spilnavn på tagget, så telefonen ikke viser tom |
| `nfc read` | Læs tag, vis UID, bundet spil og NDEF-tekst |
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
| `nfc sprog en` / `nfc lang en` | Skift sprog (`da` `en` `de` `sv` `nb` `fr`) og gem det |
| `nfc udev` | Vis udev-regel til ACR122U |
| `nfc udev install` | Installér udev-regel (pkexec, ligger i AppImage) |
| `nfc install` | AppImage: kopiér til ~/Applications, menu, udev |

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
~/Applications/nfc-games-x86_64.AppImage sprog          # liste (aktivt sprog er markeret)
~/Applications/nfc-games-x86_64.AppImage sprog en       # English
~/Applications/nfc-games-x86_64.AppImage sprog da       # Dansk
~/Applications/nfc-games-x86_64.AppImage sprog de       # Deutsch
~/Applications/nfc-games-x86_64.AppImage sprog sv       # Svenska
~/Applications/nfc-games-x86_64.AppImage sprog nb       # Norsk
~/Applications/nfc-games-x86_64.AppImage sprog fr       # Français
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
~/Applications/nfc-games-x86_64.AppImage udev install
```

Træk USB ud og sæt i igen. Grøn LED = klar. Gul + 2 beep = tag på. Grøn + 1 beep = tag af. Rød = værktøjet kører ikke.

## AppImage

Første kørsel (eller `./scripts/build-appimage.sh`) installerer AppImage til
`~/Applications/nfc-games-x86_64.AppImage`, app-menu,
og udev-reglen (adgangskode via pkexec). Tags gemmes i `~/.config/nfc-games/tags.conf`.
Watch kører **ikke** i baggrunden; vælg **1** i menuen.

```bash
~/Applications/nfc-games-x86_64.AppImage            # menu
~/Applications/nfc-games-x86_64.AppImage install
~/Applications/nfc-games-x86_64.AppImage udev install   # kun hvis pkexec blev afbrudt
~/Applications/nfc-games-x86_64.AppImage games
~/Applications/nfc-games-x86_64.AppImage watch
~/Applications/nfc-games-x86_64.AppImage restart
```

## Typisk brug

```bash
AI=~/Applications/nfc-games-x86_64.AppImage
$AI                 # menu
$AI games
$AI add "BloodRayne 2"
```

Uden argumenter åbnes kun menuen. Vælg **1** for watch i samme terminal.

- **Grøn + ét beep** — tag af, spil stoppes, scanneren lytter
- **Gul + to beep** — tag på, spil startes
- **Skift tag** — nyt tag stopper det gamle spil og starter det nye (uden grøn imellem)
- **Rød** — værktøjet kører ikke (watch er stoppet, eller USB-fejl)

Ctrl+C: LED rød.
