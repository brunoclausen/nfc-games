# nfc-games

**Download (Linux x86_64):** [release/nfc-games-x86_64.AppImage](release/nfc-games-x86_64.AppImage)  
Make it executable and run it once.

**How to use it at home (Danish):** [VEJLEDNING.md](VEJLEDNING.md)

Linux helper: tap an **ACR122U** NFC tag to start or stop a **Steam** game
(including non-Steam shortcuts from [Steam ROM Manager](https://steamgriddb.github.io/steam-rom-manager/)).

Dansk: [README.da.md](README.da.md)

**Tested on:** Bazzite (Fedora) x86_64, ACR122U firmware ACR122U216.  
**Not:** Windows, macOS, other NFC readers, or launching games outside Steam.

## Why this exists (Zaparoo/TapTo comparison)

[Zaparoo](https://zaparoo.org) (the TapTo successor) is the established
option for playing from NFC tags, but its ACR122U driver on Linux is
`libnfc`, where **the reader LED and beeper do not work and some clone
variants are incompatible** (PC/SC-only clones often fail entirely on
Linux). nfc-games instead talks to the ACR122U **directly via libusb**
(same CCID channel that PC/SC uses), so the LED and buzzer work normally
and clone variants such as the tested `ACR122U216` are supported.

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
./scripts/full-test.sh --hw   # files, langs, CLI, AppImage build, ACR122U, Steam
```

```bash
./build/nfc firmware   # expect ACR122U216 or similar
./build/nfc games
./build/nfc add 123456  # bind a tag; use appid if names clash
./build/nfc write       # write the game name onto an already bound tag
./build/nfc watch       # green = ready; tag on = yellow+2 beeps+start; tag off = 1 beep+green; not running = red
./build/nfc udev install  # ACR122U udev rule (pkexec, once)
```

LED: **green** ready, **yellow + 2 beeps** tag on, **green + 1 beep** tag off, **red** not running.

Unplug and replug the reader if the first firmware read fails.

## AppImage (optional)

A self-contained binary if you do not want to keep a source tree. First run
installs itself:

- copies itself to `~/Applications/nfc-games-x86_64.AppImage`
- app menu (`nfc menu`) plus a systemd user unit, so watch starts at login
- ACR122U udev rule (password via pkexec, once)

```bash
./scripts/build-appimage.sh
# output: dist/nfc-games-x86_64.AppImage
# also installs to ~/Applications and enables + starts watch
```

```bash
chmod +x nfc-games-x86_64.AppImage
./nfc-games-x86_64.AppImage           # install + menu
./nfc-games-x86_64.AppImage install   # install only (also puts ~/bin/nfc on PATH)
nfc                                   # after that, everything runs in the AppImage
nfc watch
nfc restart
```

## Upload changes (Gitea, free)

From this directory, after a change:

```bash
./scripts/upload.sh --build
```

That commits, pushes to the home Gitea, and puts the AppImage on the
Gitea release. Gitea runs CI (build + tests) with no GitHub billing.

- Code: http://192.168.1.3:3002/app/nfc-games
- Actions: http://192.168.1.3:3002/app/nfc-games/actions

After a green Gitea full test, code and the AppImage are published to
GitHub (git via a repo deploy key; Release via a fine-grained PAT).

One-time: add `~/.config/nfc-games/ssh/github_nfc_games.pub` as a write
deploy key on the GitHub repo, then:

```bash
./scripts/upload.sh --token ghp_YOUR_TOKEN
```

The token stays in `~/.config/nfc-games/github.token` (mode 600) and a
Gitea secret. It is not committed.

## Config

| File | Purpose |
| --- | --- |
| `~/.config/nfc-games/tags.conf` | tag UID → Steam appid (or lutris slug / heroic app_name) |
| `~/.config/nfc-games/nfc.conf` | language (`da`, `en`, `de`, `sv`, `nb`, `fr`) and optional `hook_start`/`hook_stop` |

Steam libraries and ROM shortcuts stay in Steam. The binary does **not** copy
games or tags.

Do **not** bind tags to `boot-windows` or Proton/runtime tools.

## Hooks (optional)

When `watch` starts or stops a Steam game it can run a command from `nfc.conf`:

```
hook_start=~/bin/game-on.sh
hook_stop=~/bin/game-off.sh
```

Each command runs detached via `sh -c`, with `$1` = Steam appid and `$2` = the
game name. The config is re-read on every trigger, so edits apply without a
restart. Hooks fire only from the `watch` daemon — not from `nfc start`/`nfc stop`.

## License

[MIT](LICENSE)
