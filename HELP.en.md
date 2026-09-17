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
| `nfc menu` | Short home menu (listen, bind tag, show tags). Extra tools under 10 More... |
| `nfc lyt` | Start listening in the background (green LED). Also starts at login. |
| `nfc watch` | Listen in this terminal. Tag on = yellow + 2 beeps + start; tag off = 1 beep + stop + green. |
| `nfc restart` / `nfc genstart` | Restart watch (systemd user unit, else pid) |
| `nfc games` | Auto-scan all Steam games and non-Steam shortcuts |
| `nfc start "BloodRayne 2"` | Start game via Steam (only one at a time) |
| `nfc start` | Read tag and start the bound game |
| `nfc lock` | Show which game is running/locked |
| `nfc stop` | Stop the running game |
| `nfc add "Silent Hill 4"` | Find the game, wait for a tag, bind tag → game and write the name (NDEF) |
| `nfc add 3640596865` | Same, but with appid (when several share a name) |
| `nfc add lutris <slug> [name]` | Bind tag → Lutris game (slug e.g. `hades`); starts via `lutris://` |
| `nfc lutris` | List non-Steam Lutris games (`lutris -l -o`); `nfc add lutris` with no slug picks from the list |
| `nfc add heroic <app_name> [name]` | Bind tag → Heroic game (e.g. `Control`); starts via `heroic://` |
| `nfc add action '<on> \|\| <off>'` | Bind tag → shell action (run via `sh -c` on tag on; `<off>` runs on tag off) |
| `nfc add emu '<command>'` | Bind tag → emulator game (runs on tag on; stopped automatically on tag off) |
| `nfc emu` | List emulator games under `~/Emulation/roms` (launcher + args from the SRM config); `nfc add emu` with no argument picks from the list |

Lutris and Heroic tags can only be started — there is no automatic stop. When the
tag is lifted the game keeps running until you close it manually. `nfc stop`, `nfc lock`
and hook's stop only apply to Steam games.

Action tags run one shell command (`<on>`) and an optional `<off>` command when the
tag is lifted. Emu tags run the emulator command on tag on (e.g. `dolphin -e game.iso`)
and stop the whole process group automatically on tag off.

`nfc lutris` and `nfc emu` list what is already installed. For emu games the launcher
and arguments come from Steam ROM Manager (`userConfigurations.json`), so the same
EmuDeck/ES-DE setup is reused; override with `NFC_ROMS_DIR` / `NFC_SRM_CONFIG`.
| `nfc write` | Write the bound game name onto the tag so a phone is not empty |
| `nfc read` | Read tag, show UID, bound game and NDEF text |
| `nfc list` | Show saved tags |
| `nfc remove "Silent Hill 4"` | Remove tag from the list |
| `nfc remove` | Read tag and remove it if it is saved |
| `nfc tag backup [file]` | Dump all tag pages to a `.hex` (default: `~/.config/nfc-games/tag-backups/`) |
| `nfc tag restore <file>` | Write tag pages back from a `.hex` (UID checked) |
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
# uid  action  <on command> [|| <off command>]
04AABBCCEE  action  lamp on || lamp off
# uid  emu  <emulator command>
04AABBCCFF  emu  dolphin -e ~/roms/MarioKartWii.iso
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

(the 64-bit id SRM/Steam puts in the `.desktop` file). For Steam shortcuts NFC does
not run the emulator directly. Parse new ROMs in SRM, restart Steam, then they show in
`nfc games`. To run an emulator directly (without Steam), bind your own command with
`nfc add emu`.

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
`~/Applications/nfc-games-x86_64.AppImage`, the app menu,
and the udev rule (password via pkexec). Tags are stored in `~/.config/nfc-games/tags.conf`.
Watch runs as a systemd user unit and **also starts at login**.
Turn it off with `systemctl --user disable --now nfc-games.service`.

```bash
~/Applications/nfc-games-x86_64.AppImage            # menu
~/Applications/nfc-games-x86_64.AppImage install
~/Applications/nfc-games-x86_64.AppImage udev install   # only if pkexec was cancelled
~/Applications/nfc-games-x86_64.AppImage games
~/Applications/nfc-games-x86_64.AppImage watch
~/Applications/nfc-games-x86_64.AppImage restart
```

## Typical use

```bash
AI=~/Applications/nfc-games-x86_64.AppImage
$AI                 # menu
$AI games
$AI add "BloodRayne 2"
```

With no arguments, only the menu opens. Choose **1** for watch in that terminal.

- **Green + one beep** — tag off, game stops, scanner is listening
- **Yellow + two beeps** — tag on, game starts
- **Switch tag** — a new tag stops the old game and starts the new one (no green in between)
- **Red** — tool is not running (watch stopped, or USB error)

Ctrl+C: LED red.
