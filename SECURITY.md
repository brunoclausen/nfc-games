# Security

nfc-games talks to a local ACR122U over USB and starts games through the
already-running Steam client. It does not open network ports and does not store credentials.

- Tags and language live in `~/.config/nfc-games/` on the user's machine.
- `nfc udev install` copies a udev rule with root (pkexec/sudo). Review
  `udev/99-acr122u.rules` before running it.
- The AppImage copies itself to `~/Applications` and writes a user systemd
  unit exists for optional `nfc restart`. Watch does not autostart. It does not run as root except for udev.
- `nfc stop` matches process command lines. Do not bind tags to system
  tools or `boot-windows` shortcuts.

Report issues by opening a GitHub issue on the project you cloned this from.
