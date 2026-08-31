# Contributing

This is a small Linux tool. Keep changes scoped.

- C++20, no extra frameworks
- Games must keep launching through `steam://` only
- USB code talks to ACR122U (`072f:2200`) over CCID
- Source runtime is `./build/nfc` (`cmake -S . -B build && cmake --build build`)
- AppImage (`./scripts/build-appimage.sh`) is optional packaging
- Run `ctest --test-dir build` before sending a change
- User data belongs in `~/.config/nfc-games/`, not in the repo
