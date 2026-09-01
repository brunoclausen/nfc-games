#!/usr/bin/env bash
# Full product test for nfc-games (software always; hardware with --hw).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

export PATH="/home/linuxbrew/.linuxbrew/bin:${PATH:-/usr/bin}"
export PKG_CONFIG_PATH="/home/linuxbrew/.linuxbrew/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
export NFC_SHARE="${NFC_SHARE:-$ROOT}"

HW=0
ONLY=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --hw) HW=1; shift ;;
    --only) ONLY="${2:-}"; shift 2 ;;
    -h|--help)
      echo "Brug: ./scripts/full-test.sh [--hw] [--only navn]"
      echo "Navne: files cmake langs scripts udev build appimage cli product hw steam"
      exit 0
      ;;
    *) echo "ukendt: $1" >&2; exit 2 ;;
  esac
done

if [[ -z "${CI:-}" && "$HW" -eq 0 ]]; then
  if lsusb 2>/dev/null | grep -qi '072f:2200'; then
    HW=1
  fi
fi
if [[ -n "${CI:-}${GITEA_ACTIONS:-}${GITHUB_ACTIONS:-}" ]]; then
  HW=0
fi

PASS=0
FAIL=0
section=""

start() {
  section="$1"
  echo
  echo "========================================"
  echo "  $1"
  echo "========================================"
}

ok() {
  echo "  OK    $*"
  PASS=$((PASS + 1))
}

bad() {
  echo "  FAIL  $*"
  FAIL=$((FAIL + 1))
}

want() {
  if [[ -z "$ONLY" || "$ONLY" == "$1" ]]; then
    return 0
  fi
  return 1
}

run_files() {
  start "1. Filer"
  local f
  for f in \
    CMakeLists.txt VERSION LICENSE README.md README.da.md VEJLEDNING.md \
    cpp/main.cpp cpp/app.cpp cpp/watch.cpp cpp/cmds.cpp cpp/menu.cpp \
    cpp/acr122.cpp cpp/tags.cpp cpp/steam.cpp cpp/i18n.cpp \
    tests/test_nfc.cpp tests/check_langs.py \
    packaging/AppRun packaging/install-udev.sh packaging/nfc-games.desktop \
    packaging/nfc-games.png packaging/nfc-games.svg \
    scripts/build-appimage.sh scripts/upload.sh scripts/full-test.sh \
    udev/99-acr122u.rules \
    lang/langs.txt lang/da.txt lang/en.txt lang/de.txt lang/sv.txt lang/nb.txt lang/fr.txt \
    .gitea/workflows/ci.yml
  do
    if [[ -e "$ROOT/$f" ]]; then ok "$f"
    else bad "mangler $f"
    fi
  done
  for f in packaging/AppRun packaging/install-udev.sh scripts/build-appimage.sh scripts/upload.sh scripts/full-test.sh; do
    if [[ -x "$ROOT/$f" ]]; then ok "$f kørbar"
    else bad "$f er ikke kørbar"
    fi
  done
}

run_cmake_sources() {
  start "2. CMake-kilder"
  local src
  while IFS= read -r src; do
    src="${src##*/}"
    if grep -q "cpp/${src}" "$ROOT/CMakeLists.txt"; then
      ok "CMakeLists har cpp/$src"
    else
      bad "CMakeLists mangler cpp/$src"
    fi
  done < <(find "$ROOT/cpp" -name '*.cpp' | sort)
  if grep -q 'tests/test_nfc.cpp' "$ROOT/CMakeLists.txt"; then
    ok "CMakeLists har tests"
  else
    bad "CMakeLists mangler tests"
  fi
}

run_langs() {
  start "3. Sprogfiler"
  if python3 "$ROOT/tests/check_langs.py" "$ROOT/lang"; then
    ok "alle 6 sprog har samme nøgler som en.txt"
    ok "C++ t(\"...\") nøgler findes i en.txt"
  else
    bad "sprogfiler eller C++-nøgler stemmer ikke"
  fi
  local n
  n="$(grep -cE '^[a-z]{2} ' "$ROOT/lang/langs.txt" || true)"
  if [[ "$n" -ge 6 ]]; then ok "langs.txt har $n sprog"
  else bad "langs.txt har kun $n sprog"
  fi
}

run_scripts() {
  start "4. Script-syntaks"
  local f
  for f in scripts/*.sh packaging/AppRun packaging/install-udev.sh; do
    if bash -n "$ROOT/$f" 2>/tmp/nfc-bashn.err; then
      ok "bash -n $f"
    else
      bad "bash -n $f"
      cat /tmp/nfc-bashn.err >&2 || true
    fi
  done
  rm -f /tmp/nfc-bashn.err
}

run_udev_packaging() {
  start "5. Udev og packaging"
  if grep -q '072f' "$ROOT/udev/99-acr122u.rules" && grep -q '2200' "$ROOT/udev/99-acr122u.rules"; then
    ok "udev-regel nævner ACR122U 072f:2200"
  else
    bad "udev-regel mangler 072f:2200"
  fi
  if grep -q 'pn533_usb' "$ROOT/udev/99-acr122u.rules"; then
    ok "udev unbinder pn533_usb"
  else
    bad "udev mangler pn533_usb-unbind"
  fi
  if grep -q '^Name=NFC Games' "$ROOT/packaging/nfc-games.desktop"; then
    ok "desktop-fil Name="
  else
    bad "desktop-fil"
  fi
  if grep -qi 'steam' "$ROOT/README.md" && grep -q 'ACR122U' "$ROOT/README.md"; then
    ok "README nævner Steam + ACR122U"
  else
    bad "README mangler Steam/ACR122U"
  fi
  local ver
  ver="$(tr -d '[:space:]' < "$ROOT/VERSION")"
  if [[ "$ver" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then ok "VERSION $ver"
  else bad "VERSION '$ver'"
  fi
}

run_build() {
  start "6. CMake + unit tests"
  cmake -S "$ROOT" -B "$ROOT/build" -DCMAKE_BUILD_TYPE=Release >/tmp/nfc-cmake.log 2>&1 || {
    bad "cmake configure"
    tail -20 /tmp/nfc-cmake.log >&2
    return
  }
  ok "cmake configure"
  cmake --build "$ROOT/build" -j "$(nproc)" >/tmp/nfc-build.log 2>&1 || {
    bad "cmake build"
    tail -30 /tmp/nfc-build.log >&2
    return
  }
  ok "cmake build"
  if ctest --test-dir "$ROOT/build" --output-on-failure >/tmp/nfc-ctest.log 2>&1; then
    ok "ctest"
  else
    bad "ctest"
    cat /tmp/nfc-ctest.log >&2
  fi
}

run_appimage() {
  start "7. AppImage"
  local app="$ROOT/dist/nfc-games-x86_64.AppImage"
  local log="/tmp/nfc-appimage.log"
  if [[ ! -x "$ROOT/scripts/build-appimage.sh" ]]; then
    bad "scripts/build-appimage.sh mangler"
    return
  fi
  if [[ ! -e /usr/lib64/libusb-1.0.so.0 && ! -e /usr/lib/x86_64-linux-gnu/libusb-1.0.so.0 ]]; then
    bad "system libusb-1.0.so.0 mangler (AppImage)"
    return
  fi
  echo "  bygger AppImage (uden at installere/genstarte watch)..."
  if NFC_AUTO_INSTALL=0 "$ROOT/scripts/build-appimage.sh" >"$log" 2>&1; then
    ok "build-appimage.sh"
  else
    bad "build-appimage.sh"
    tail -40 "$log" >&2
    return
  fi
  if [[ -x "$app" ]]; then
    ok "dist/nfc-games-x86_64.AppImage kørbar"
  else
    bad "dist/nfc-games-x86_64.AppImage mangler"
    return
  fi
  local kind
  kind="$(file -b "$app" 2>/dev/null || true)"
  if echo "$kind" | grep -qi 'ELF'; then
    ok "AppImage er ELF ($kind)"
  else
    bad "AppImage file: $kind"
  fi
  local ver out
  ver="$(tr -d '[:space:]' < "$ROOT/VERSION")"
  out="$(APPIMAGE_EXTRACT_AND_RUN=1 NFC_SKIP_INSTALL=1 "$app" version 2>/dev/null || true)"
  if [[ "$out" == *"$ver"* ]]; then
    ok "AppImage version $ver"
  else
    bad "AppImage version '$out' != $ver"
  fi
}

run_cli() {
  start "8. CLI"
  local nfc="$ROOT/build/nfc"
  if [[ ! -x "$nfc" ]]; then
    bad "build/nfc mangler (kør cmake-trin)"
    return
  fi
  local ver out
  ver="$(tr -d '[:space:]' < "$ROOT/VERSION")"
  out="$("$nfc" version 2>/dev/null || true)"
  if [[ "$out" == *"$ver"* ]]; then ok "nfc version = $ver"
  else bad "nfc version '$out' != $ver"
  fi
  out="$("$nfc" --help 2>/dev/null || true)"
  if [[ "$out" == *watch* && "$out" == *restart* && "$out" == *ACR122U* ]]; then ok "nfc --help"
  else bad "nfc --help"
  fi
  out="$(NFC_LANG=en "$nfc" sprog 2>/dev/null || true)"
  if echo "$out" | grep -q '^da ' && echo "$out" | grep -q '^en '; then
    ok "nfc sprog lister da/en"
  else
    bad "nfc sprog"
  fi
  out="$("$nfc" udev 2>/dev/null || true)"
  if echo "$out" | grep -q '072f'; then ok "nfc udev viser regel"
  else bad "nfc udev"
  fi
}

run_product() {
  start "9. Installeret produkt"
  local app="$HOME/Applications/nfc-games-x86_64.AppImage"
  if [[ -x "$app" ]]; then
    ok "AppImage $app"
    local ver out
    ver="$(tr -d '[:space:]' < "$ROOT/VERSION")"
    out="$(NFC_SKIP_INSTALL=1 "$app" version 2>/dev/null || true)"
    if [[ "$out" == *"$ver"* ]]; then ok "AppImage version $ver"
    else bad "AppImage version '$out'"
    fi
  else
    echo "  SKIP  AppImage (ikke installeret her)"
  fi
  if [[ -f /etc/udev/rules.d/99-acr122u.rules ]]; then
    ok "udev installeret i /etc"
  else
    echo "  SKIP  /etc/udev/rules.d/99-acr122u.rules"
  fi
  if systemctl --user is-active nfc-games.service >/dev/null 2>&1; then
    ok "systemd nfc-games.service active"
  else
    echo "  SKIP  nfc-games.service ikke aktiv"
  fi
  if [[ -f "${XDG_CONFIG_HOME:-$HOME/.config}/nfc-games/tags.conf" ]]; then
    local n
    n="$(grep -cE '^[0-9A-Fa-f]' "${XDG_CONFIG_HOME:-$HOME/.config}/nfc-games/tags.conf" || true)"
    if [[ "$n" -ge 1 ]]; then ok "tags.conf har $n tags"
    else bad "tags.conf er tom"
    fi
  else
    echo "  SKIP  ingen tags.conf"
  fi
}

run_hw() {
  start "10. Hardware (ACR122U)"
  if [[ "$HW" -ne 1 ]]; then
    echo "  SKIP  hardware (CI eller ingen læser). Brug --hw"
    return
  fi
  if ! lsusb 2>/dev/null | grep -qi '072f:2200'; then
    bad "ACR122U 072f:2200 ikke på USB"
    return
  fi
  ok "USB 072f:2200"
  local nfc="$ROOT/build/nfc"
  [[ -x "$nfc" ]] || nfc="$HOME/Applications/nfc-games-x86_64.AppImage"
  if [[ ! -x "$nfc" ]]; then
    bad "ingen nfc-binær til firmware-test"
    return
  fi
  local fw
  if [[ "$nfc" == *.AppImage ]]; then
    fw="$(NFC_SKIP_INSTALL=1 "$nfc" firmware 2>/dev/null || true)"
  else
    fw="$("$nfc" firmware 2>/dev/null || true)"
  fi
  if echo "$fw" | grep -qi 'ACR122'; then
    ok "firmware $fw"
  else
    bad "firmware '$fw'"
  fi
  if pgrep -f 'nfc watch' >/dev/null 2>&1 || pgrep -f 'nfc-games-x86_64.AppImage watch' >/dev/null 2>&1; then
    ok "watch kører"
  else
    echo "  SKIP  watch kører ikke (startes kun fra menu 1, ikke i baggrunden)"
  fi
  local list
  if [[ "$nfc" == *.AppImage ]]; then
    list="$(NFC_SKIP_INSTALL=1 "$nfc" list 2>/dev/null || true)"
  else
    list="$("$nfc" list 2>/dev/null || true)"
  fi
  if echo "$list" | grep -qE '[0-9A-Fa-f]{8,}'; then
    ok "nfc list viser tags"
  else
    echo "  SKIP  nfc list (ingen tags eller tom)"
  fi
}

run_games() {
  start "11. Steam"
  if [[ -n "${CI:-}${GITEA_ACTIONS:-}${GITHUB_ACTIONS:-}" ]]; then
    echo "  SKIP  Steam-scan i CI"
    return
  fi
  local nfc="$ROOT/build/nfc"
  [[ -x "$nfc" ]] || return
  local out
  out="$("$nfc" games 2>/dev/null | tail -5 || true)"
  if echo "$out" | grep -qE '[0-9]+ games|[0-9]+ spil'; then
    ok "nfc games: $out"
  elif echo "$out" | grep -qi 'no Steam'; then
    echo "  SKIP  Steam ikke fundet"
  else
    # last line might be the count
    if [[ -n "$out" ]]; then ok "nfc games kørte"
    else bad "nfc games tom"
    fi
  fi
}

want files && run_files
want cmake && run_cmake_sources
want langs && run_langs
want scripts && run_scripts
want udev && run_udev_packaging
want build && run_build
want appimage && run_appimage
want cli && run_cli
want product && run_product
want hw && run_hw
want steam && run_games

echo
echo "========================================"
echo "  Full test: $PASS OK, $FAIL FAIL"
echo "========================================"
if [[ "$FAIL" -ne 0 ]]; then
  exit 1
fi
echo "  All nfc-games tests passed!"
exit 0
