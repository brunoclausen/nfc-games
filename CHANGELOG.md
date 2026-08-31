# Changelog

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
