# nfc-games

Linux helper: tap an **ACR122U** NFC tag to start or stop a **Steam** game
(including non-Steam shortcuts from [Steam ROM Manager](https://steamgriddb.github.io/steam-rom-manager/)).

Dansk: [README.da.md](README.da.md)

**Tested on:** Bazzite (Fedora) x86_64, ACR122U firmware ACR122U216.  
**Not:** Windows, macOS, other NFC readers, or launching games outside Steam.

## Requirements

- Linux x86_64
- Steam running on the same machine
- ACR122U USB reader (`072f:2200`)
- Games you want on tags must already be in Steam (installed or non-Steam shortcut)
- To build: C++20, CMake ≥ 3.16, pkg-config, libusb-1.0 development files

## Build and run (C++)

This is the source runtime: `./build/nfc`.

```bash
# Fedora / Bazzite
sudo dnf install gcc-c++ cmake pkgconf-pkg-config libusb1-devel
# Debian / Ubuntu
# sudo apt install build-essential cmake pkg-config libusb-1.0-0-dev

cmake -S . -B build && cmake --build build
ctest --test-dir build
```

```bash
./build/nfc firmware   # expect ACR122U216 or similar
./build/nfc games
./build/nfc add 123456  # bind a tag; use appid if names clash
./build/nfc watch       # green = ready; tag on = yellow+2 beeps+start; tag off = 1 beep+green; not running = red
./build/nfc udev install  # ACR122U udev rule (pkexec, once)
```

LED: **green** ready, **yellow + 2 beeps** tag on, **green + 1 beep** tag off, **red** not running.

Unplug and replug the reader if the first firmware read fails.

## AppImage (optional)

A self-contained binary if you do not want to keep a source tree. First run
installs itself:

- copies itself to `~/Applications/nfc-games-x86_64.AppImage`
- app menu (`nfc menu`); `watch` at login via a user systemd service
- ACR122U udev rule (password via pkexec, once)

```bash
./scripts/build-appimage.sh
# output: dist/nfc-games-x86_64.AppImage
# also installs to ~/Applications and restarts watch
```

```bash
chmod +x nfc-games-x86_64.AppImage
./nfc-games-x86_64.AppImage           # install + watch
./nfc-games-x86_64.AppImage install   # install only
./nfc-games-x86_64.AppImage watch
```

## Upload changes to GitHub

From this directory, after a change:

```bash
./scripts/upload-github.sh --build
```

That commits, pushes, and puts the AppImage on the GitHub Release.

First time only:

```bash
./scripts/upload-github.sh --token ghp_YOUR_TOKEN
```

Create a classic token (scope `repo` only) under GitHub → Settings →
Developer settings → Personal access tokens. It is stored in
`~/.config/nfc-games/github.token` and is not committed.

## Config

| File | Purpose |
| --- | --- |
| `~/.config/nfc-games/tags.conf` | tag UID → Steam appid |
| `~/.config/nfc-games/nfc.conf` | language (`da`, `en`, `de`, `sv`, `nb`, `fr`) |

Steam libraries and ROM shortcuts stay in Steam. The binary does **not** copy
games or tags.

Do **not** bind tags to `boot-windows` or Proton/runtime tools.

## License

[MIT](LICENSE)
