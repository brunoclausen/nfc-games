# nfc-games help

C++ program for ACR122U: LED, beep, NFC tags and Steam games.

Run the built binary `./build/nfc`. The AppImage is optional.

```bash
cmake -S . -B build && cmake --build build
./build/nfc --help
./build/nfc help
./build/nfc watch
```

## Commands

| Command | What it does |
| --- | --- |
| `nfc menu` | Interactive menu (numbered) |
| `nfc watch` | Tag on = yellow + 2 beeps + start game; tag off = 1 beep + stop + green |
| `nfc games` | Auto-scan all Steam games and non-Steam shortcuts |
| `nfc start "BloodRayne 2"` | Start game via Steam (only one at a time) |
| `nfc start` | Read tag and start the bound game |
| `nfc lock` | Show which game is running/locked |
| `nfc stop` | Stop the running game |
| `nfc add "Silent Hill 4"` | Find the game, wait for a tag, bind tag → game |
| `nfc add 3640596865` | Same, but with appid (when several share a name) |
| `nfc read` | Read tag, show UID and bound game |
| `nfc list` | Show saved tags |
| `nfc remove "Silent Hill 4"` | Remove tag from the list |
| `nfc remove` | Read tag and remove it if it is saved |
| `nfc farver` | Test red, green, yellow LED + beep |
| `nfc led green\|red\|yellow\|off` | Set LED |
| `nfc beep 300` | Beep (ms) |
| `nfc firmware` | Show ACR122U firmware |
| `nfc --help` / `nfc -h` | Short help in the terminal |
| `nfc help` | This help (HELP.md / HELP.en.md) |
| `nfc lang` / `nfc sprog` | List languages and the active one |
| `nfc lang en` / `nfc sprog en` | Switch language (`da` `en` `de` `sv` `nb` `fr`) and save it |
| `nfc udev` | Show udev rule for ACR122U |
| `nfc udev install` | Install udev rule (pkexec, bundled in the AppImage) |
| `nfc install` | AppImage: copy to ~/Applications, menu, udev |

## Tag list

Saved tags live in:

`~/.config/nfc-games/tags.conf`

(`$NFC_TAGS` overrides. The first run copies the old `~/nfc-games/tags.conf` if it exists.)

Format:

```
# uid  kind  appid  name
04AABBCCDD  steam  1373550  BloodRayne 2: Terminal Cut
```

The file is created the first time a tag is saved with `nfc add`.

## Language

CLI text can be switched:

```bash
~/Applications/nfc-games-x86_64.AppImage lang          # list (active language is marked)
~/Applications/nfc-games-x86_64.AppImage lang en       # English
~/Applications/nfc-games-x86_64.AppImage lang da       # Danish
```

`nfc sprog` is the same. The choice is saved in `~/.config/nfc-games/nfc.conf`.  
`$NFC_LANG` (e.g. `en` or `da`) overrides the file for one run.

## Steam list

No fixed game file. `nfc games` scans every time:

- Steam root (`~/.steam/steam`, `~/.local/share/Steam`)
- `libraryfolders.vdf` + `appmanifest_*.acf` (installed Steam games)
- `shortcuts.vdf` — emu games from **Steam ROM Manager**
  (https://steamgriddb.github.io/steam-rom-manager/)

SRM writes shortcuts into Steam (PCSX2, Ryujinx, ES-DE, …). NFC reads the same
shortcuts and starts them only via Steam, e.g.:

```
steam steam://rungameid/15636244473128681472
```

(the 64-bit id SRM/Steam puts in the `.desktop` file). NFC does not run the
emulator directly. Parse new ROMs in SRM, restart Steam, then they show in
`nfc games`.

Proton and Steam Runtime are hidden.

All games **and** emu shortcuts start only via Steam (`steam steam://rungameid/...`).

If two games share a name (e.g. Batman), use **appid** with `nfc add`.

## Hardware

Reader: ACR122U (USB `072f:2200`). LED is two-color: red, green, yellow (both).

First time, one udev rule so kernel NFC does not steal the reader:

```bash
~/Applications/nfc-games-x86_64.AppImage udev install
```

Unplug USB and plug it back in. Green LED = ready. Yellow + 2 beeps = tag on. Green + 1 beep = tag off. Red = tool not running.

## AppImage

The first run (or `./scripts/build-appimage.sh`) installs the AppImage to
`~/Applications/nfc-games-x86_64.AppImage`, the app menu, `watch` at login (systemd),
and the udev rule (password via pkexec). Tags are stored in `~/.config/nfc-games/tags.conf`.

```bash
~/Applications/nfc-games-x86_64.AppImage install
~/Applications/nfc-games-x86_64.AppImage udev install   # only if pkexec was cancelled
~/Applications/nfc-games-x86_64.AppImage games
~/Applications/nfc-games-x86_64.AppImage watch
```

## Typical use

```bash
AI=~/Applications/nfc-games-x86_64.AppImage
$AI games
$AI add "BloodRayne 2"
$AI watch
```

`nfc watch` runs until Ctrl+C.

- **Green + one beep** — tag off, game stops, scanner is listening
- **Yellow + two beeps** — tag on, game starts
- **Red** — tool is not running (watch stopped, or USB error)

Ctrl+C: LED red.
