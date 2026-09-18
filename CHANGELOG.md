# Changelog

## 1.0.13

- Menu: "Emulator games" and "Lutris games" bind a tag directly (no extra prompt)
- Emulator picker: pick the system first, then the game
- Amiga (and other EmuDeck/RetroArch parsers): `${os:...}`, `${racores}` and
  `${/}` macros are now resolved, so games launch with the correct core
  (`retroarch.sh -L …/cores/puae_libretro.so '<rom>'`). Unknown macros are
  dropped instead of throwing away the whole command line.
- `~/.config/steam-rom-manager`: "Amiga - RetroArch PUAE" parser enabled;
  added "Amiga - FS-UAE" parser and a `fs-uae.sh` launcher
- Fix: emulator scan could busy-loop when a directory disappeared mid-scan
- Fix: a malformed `usb.pause` file or a dead pauser no longer parks the
  watcher forever; the file is removed and the watcher resumes
- Fix: hook commands run as a detached double-fork, so they never leave zombies
- Fix: `nfc lutris-install` quotes the slug; progress lines that overwrite with
  `\r` are shown live instead of sticking in a buffer
- Fix: `nfc led` without a colour and unknown commands fail with usage before
  the reader is touched
- Fix: writing a tag no longer wipes a different physical tag that happens to
  share the same display name; `nfc remove 0` no longer removes every non-Steam
  tag

## 1.0.12

- Home menu: listen, bind tag, show/read/remove tags, write name, language
- Bind a tag by picking a number from the Steam game list
- Watch starts at login (systemd enabled); opening the menu no longer stops it
- Desktop name/actions in Danish; udev/firmware moved under More...
- Fix: `AppId=` was matched as a substring, so stopping appid 220 could kill
  appid 2200. It is now matched as a whole number.
- Fix: an unbound tag left on the reader logged and re-read `tags.conf` on
  every poll, which also starved the RF keep-alive. Retries every 3s, logs once.
- Fix: `#` in a game name truncated the tag name, and a leading `#` (e.g.
  "#DRIVE") threw and killed the watch daemon. `#` now only comments out a
  whole line, and one bad line no longer discards the whole file.
- Fix: a corrupt `shortcuts.vdf` could recurse the stack to death; the binary
  VDF and NDEF parsers now have depth limits.
- Fix: the menu closed instead of reporting the error on any non-reader
  exception; watch survives a bad `tags.conf` or a failed fork.
- Hooks: command lines in the config run on game start/stop and on switch
  (e.g. LED scripts or announcements)
- Lutris and Heroic games: `nfc add lutris <slug>` / `nfc add heroic <app_name>`
  binds a tag to a non-Steam launcher game (starts via `lutris://`/`heroic://`,
  no automatic stop)
- Action tags: `nfc add action '<on> || <off>'` binds a tag to shell command(s)
  (the optional off command runs when the tag is lifted); `nfc start action
  <command>` runs one now without a tag. Menu: "Bind a new tag to an action".
- Emulator tags: `nfc add emu '<command>'` runs any emulator directly (e.g.
  Dolphin, RetroArch) on tag on and stops the whole process group on tag off;
  `nfc start emu <command>` starts one now. Menu: "Bind a new tag to an emulator
  game". Steam ROM Manager shortcuts keep launching via Steam.
- `nfc lutris` lists non-Steam Lutris games (`lutris -l -o`); `nfc add lutris`
  with no slug picks from a numbered list. Menu item 5: "Bind a new tag to a
  Lutris game".
- `nfc emu` lists emulator games under `~/Emulation/roms`, taking launcher and
  arguments from the Steam ROM Manager config (`userConfigurations.json`) so the
  EmuDeck/ES-DE setup is reused; `nfc add emu` with no argument picks from the
  list. Override with `NFC_ROMS_DIR` / `NFC_SRM_CONFIG`.
- Emulator picker: when a system has several enabled parsers (e.g. Switch with
  Citron/Eden/Ryujinx), the one whose launcher is actually installed now wins,
  instead of the first in the config.

## 1.0.11

- Watch stays running if the ACR122U is missing (wait/retry instead of exit)
- Periodic RF field cycle instead of USB reopen every 15 minutes (firmware hang)
- Presence check while a tag is held uses Get UID only (no InList every poll)
- LED APDUs only on colour change (was every poll)
- `libusb_reset_device` at most once per 10 minutes, after repeated USB reopens
- udev installer blacklists `pn533_usb` so the kernel NFC stack cannot grab the reader
- systemd unit: RestartSec=15 and a start limit (watch still does not autostart)
- AppImage install puts `nfc` on PATH (`~/bin/nfc` → AppImage) and refreshes staged udev files on upgrade

## 1.0.10

- `nfc tag backup` / `nfc tag restore` for full NTAG page dumps
- Container AppImage build targeting glibc ≤ 2.34

## 1.0.9

- Stop sending PN532 `SAMConfiguration` (it froze ACR122U firmware)
- Do not `libusb_reset_device` on every close (that killed the host xHCI controller)

## 1.0.7

- Type 2 NDEF read/write on ACR122U also uses raw Ultralight commands (Fudan NTAG clones)
- `nfc write` no longer reports "not NTAG" when the tag is Ultralight but ACS PICC read fails

## 1.0.6

- Write the game name as NDEF text onto NTAG/Ultralight tags (`nfc add` and `nfc write`)
- `nfc read` shows NDEF so a phone no longer looks at an empty tag after write
- Menu item 19 writes the bound name onto a tag already in tags.conf

## 1.0.5

- ACR122U USB reset after a long idle, after tag-off, and when switching tags
- Stop hammering recover() every few seconds while idle (that made long runs worse)
- Slower polling after 30s idle so the reader stays healthy

## 1.0.4

- Watch does not run in the background or at login
- AppImage with no args only opens the menu; choose 1 for watch in that terminal

## 1.0.3

- Running the AppImage with no args starts watch in the background, then opens the menu
- Menu item 1 starts background watch instead of blocking the terminal

## 1.0.2

- Faster tag switching: a new UID starts the next game without waiting for tag-off
- Empty reads during a swap no longer drop the new-tag candidate
- Watch keeps polling while a tag is on (no 2s PICC pause) and reactivates to see a new tag

## 1.0.1

- `nfc restart` / `nfc genstart` restarts watch (systemd user unit, else pidfile)
- Menu item 18 and a desktop action for restart
- After `nfc sprog`, print a hint to run `nfc restart` so watch picks up the language

## 1.0.0

First public release.

- ACR122U over USB CCID (no pcscd / libnfc)
- Bind NFC tags to Steam games and non-Steam shortcuts
- Launch and stop only through `steam://rungameid/...`
- Translations in `lang/*.txt`: Danish, English, German, Swedish, Norwegian, French
- CLI language follows `nfc sprog`, `NFC_LANG`, then the system locale
- AppImage with bundled libusb, udev rule, and first-run installer
- Tags and language stored in `~/.config/nfc-games/`
- Watch LED: yellow + two beeps on tag, green + one beep off tag, red when not running
- AppImage auto-installs to `~/Applications`, app menu, and udev
- Single login starter: systemd user unit (not also XDG autostart)
- Watch recovers USB/RF when the ACR122U stops seeing tags after idle
- Do not stop a running game on brief NFC misses while the tag is still on
- Keep LED yellow and the game running for as long as the tag stays on
- Idle watch keeps LED green (PICC polling no longer leaves it red)
