# nfc-games

Linux helper: tap an **ACR122U** NFC tag to start or stop a **Steam** game
(including non-Steam shortcuts from [Steam ROM Manager](https://steamgriddb.github.io/steam-rom-manager/)).

Dansk: [README.da.md](README.da.md)

**Tested on:** Bazzite (Fedora) x86_64, ACR122U firmware ACR122U216.  
**Not:** Windows, macOS, other NFC readers, or launching games outside Steam.

## Requirements

- Linux x86_64 (glibc recent enough for a Fedora/Bazzite AppImage)
- Steam running on the same machine
- ACR122U USB reader (`072f:2200`)
- Games you want on tags must already be in Steam (installed or non-Steam shortcut)

## AppImage

This is the only runtime. Do not run `./build/nfc`.

Run the AppImage once. It installs itself:

- always copies itself to `~/Applications/nfc-games-x86_64.AppImage`
- app menu (`nfc menu`); `watch` at login via a user systemd service
- ACR122U udev rule (password via pkexec, once)

```bash
chmod +x nfc-games-x86_64.AppImage
./nfc-games-x86_64.AppImage           # install + watch
./nfc-games-x86_64.AppImage install   # install only
```

Building with `./scripts/build-appimage.sh` also runs `install` on this PC.
Unplug and replug the reader if the first firmware read fails.

```bash
./nfc-games-x86_64.AppImage firmware   # expect ACR122U216 or similar
./nfc-games-x86_64.AppImage games
./nfc-games-x86_64.AppImage add 123456  # bind a tag; use appid if names clash
./nfc-games-x86_64.AppImage watch       # green = ready; tag on = yellow+2 beeps+start; tag off = 1 beep+green; not running = red
./nfc-games-x86_64.AppImage udev install  # only if the automatic install did not run
```

LED: **green** ready, **yellow + 2 beeps** tag on, **green + 1 beep** tag off, **red** not running.

## Config (not inside the AppImage)

| File | Purpose |
| --- | --- |
| `~/.config/nfc-games/tags.conf` | tag UID → Steam appid |
| `~/.config/nfc-games/nfc.conf` | language (`da`, `en`, `de`, `sv`, `nb`, `fr`) |

Steam libraries and ROM shortcuts stay in Steam. Copying the AppImage does
**not** copy games or tags.

Do **not** bind tags to `boot-windows` or Proton/runtime tools.

## Build the AppImage

Need C++20, CMake ≥ 3.16, pkg-config, and libusb-1.0 development files.

```bash
# Fedora / Bazzite
sudo dnf install gcc-c++ cmake pkgconf-pkg-config libusb1-devel
# Debian / Ubuntu
# sudo apt install build-essential cmake pkg-config libusb-1.0-0-dev

./scripts/build-appimage.sh
# output: dist/nfc-games-x86_64.AppImage
# also installs to ~/Applications and restarts watch
```

Tests (not the runtime):

```bash
cmake -S . -B build && cmake --build build && ctest --test-dir build
```

## License

[MIT](LICENSE)
