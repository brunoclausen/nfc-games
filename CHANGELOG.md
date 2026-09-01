# Changelog

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
