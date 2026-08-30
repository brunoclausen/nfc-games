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
| `nfc help` | Denne hjælp (HELP.md) |

## Tag-liste

Gemte tags ligger i:

`~/nfc-games/tags.conf`

(filen i den mappe, du kører `nfc` fra. Eller `$NFC_TAGS` hvis den er sat.)

Format:

```
# uid  kind  appid  name
04AABBCCDD  steam  1373550  BloodRayne 2: Terminal Cut
```

Filen oprettes først, når et tag bliver gemt med `nfc add`.

## Steam-liste

Ingen fast spillefil. `nfc games` scanner selv hver gang:

- Steam-rod (`~/.steam/steam`, `~/.local/share/Steam`)
- `libraryfolders.vdf` + `appmanifest_*.acf` (installerede Steam-spil)
- `shortcuts.vdf` (non-Steam: emulation, Ryujinx, ES-DE, …)

Proton og Steam Runtime vises ikke.

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

## Typisk brug

```bash
cd ~/nfc-games
./build/nfc games
./build/nfc add "BloodRayne 2"
./build/nfc watch
```

`nfc watch` kører indtil Ctrl+C. Grøn = intet tag. Gul + ét beep = tag på og spil startes. Tag af = spil stoppes og LED bliver grøn.
