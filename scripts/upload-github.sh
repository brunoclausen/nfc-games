#!/usr/bin/env bash
# Commit local changes, push to GitHub, and put the AppImage on the release.
# One-time: put a classic token (scope: repo) in ~/.config/nfc-games/github.token
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

CONF="${XDG_CONFIG_HOME:-$HOME/.config}/nfc-games"
TOKEN_FILE="$CONF/github.token"
OWNER_REPO="${NFC_GITHUB_REPO:-brunoclausen/nfc-games}"
APP_NAME="nfc-games-x86_64.AppImage"
APP="$ROOT/dist/$APP_NAME"
API="https://api.github.com"
NEED_BUILD=0
MSG=""

usage() {
  cat <<'EOF'
Brug:
  ./scripts/upload-github.sh
  ./scripts/upload-github.sh --build
  ./scripts/upload-github.sh --build "kort besked om ændringen"

Første gang (én gang):
  1. github.com → Settings → Developer settings → Personal access tokens
     → Tokens (classic) → Generate new token
  2. Sæt kryds kun ved: repo
  3. Gem nøglen med:

       ./scripts/upload-github.sh --token ghp_DIN_NØGLE

Derefter, efter hver ændring:
  ./scripts/upload-github.sh --build

  --build   byg AppImage først (når du har ændret programmet)
  --token   gem GitHub-nøglen (kun første gang)
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -h|--help) usage; exit 0 ;;
    --build) NEED_BUILD=1; shift ;;
    --token)
      if [[ -z "${2:-}" ]]; then
        echo "nfc: --token kræver nøglen (ghp_...)" >&2
        exit 2
      fi
      mkdir -p "$CONF"
      umask 077
      printf '%s\n' "$2" > "$TOKEN_FILE"
      chmod 600 "$TOKEN_FILE"
      echo "nfc: nøgle gemt i $TOKEN_FILE"
      shift 2
      ;;
    --) shift; break ;;
    -*)
      echo "nfc: ukendt flag: $1" >&2
      usage >&2
      exit 2
      ;;
    *)
      if [[ -n "$MSG" ]]; then MSG="$MSG $1"; else MSG="$1"; fi
      shift
      ;;
  esac
done

load_token() {
  local raw=""
  if [[ -n "${GITHUB_TOKEN:-}" ]]; then
    raw="$GITHUB_TOKEN"
  elif [[ -n "${GH_TOKEN:-}" ]]; then
    raw="$GH_TOKEN"
  elif [[ -f "$TOKEN_FILE" ]]; then
    raw="$(tr -d '[:space:]' < "$TOKEN_FILE")"
  fi
  if [[ -z "$raw" || "$raw" != ghp_* ]]; then
    echo "nfc: mangler GitHub-nøgle." >&2
    usage >&2
    exit 1
  fi
  TOKEN="$raw"
}

auth_curl() {
  curl -sS \
    -H "Authorization: Bearer ${TOKEN}" \
    -H "Accept: application/vnd.github+json" \
    -H "X-GitHub-Api-Version: 2022-11-28" \
    "$@"
}

check_token() {
  local login
  login="$(auth_curl "$API/user" | python3 -c 'import json,sys; print(json.load(sys.stdin).get("login",""))')"
  if [[ -z "$login" ]]; then
    echo "nfc: GitHub-nøglen virker ikke. Lav en ny (kun repo) og gem den i $TOKEN_FILE" >&2
    exit 1
  fi
  echo "nfc: GitHub-bruger $login"
}

push_github() {
  unset GIT_ASKPASS SSH_ASKPASS DISPLAY || true
  export GIT_TERMINAL_PROMPT=0
  git -c credential.helper= push \
    "https://x-access-token:${TOKEN}@github.com/${OWNER_REPO}.git" \
    "$@"
}

VERSION="$(tr -d '[:space:]' < "$ROOT/VERSION")"
TAG="v${VERSION}"
if [[ -z "$MSG" ]]; then
  MSG="Opdatering ${VERSION} ($(date +%Y-%m-%d))"
fi

load_token
check_token
mkdir -p "$CONF"
if [[ ! -f "$TOKEN_FILE" ]]; then
  umask 077
  printf '%s\n' "$TOKEN" > "$TOKEN_FILE"
  chmod 600 "$TOKEN_FILE"
  echo "nfc: nøgle gemt i $TOKEN_FILE"
fi

if [[ "$NEED_BUILD" -eq 1 ]]; then
  echo "nfc: bygger AppImage..."
  "$ROOT/scripts/build-appimage.sh"
fi

echo "nfc: git..."
git add -A
if git diff --cached --quiet && git diff --quiet; then
  echo "nfc: ingen nye filer at committe"
else
  git commit -m "$MSG"
  echo "nfc: commit: $MSG"
fi

if git remote get-url origin >/dev/null 2>&1; then
  git push origin HEAD:main || echo "nfc: hjemmeserver (origin) kunne ikke pushe — fortsætter med GitHub" >&2
fi

if ! git remote get-url github >/dev/null 2>&1; then
  git remote add github "https://github.com/${OWNER_REPO}.git"
fi

push_github HEAD:main
echo "nfc: kode på GitHub (main)"

if git rev-parse "$TAG" >/dev/null 2>&1; then
  push_github "$TAG" || true
else
  git tag -a "$TAG" -m "nfc-games ${VERSION}"
  push_github "$TAG"
  echo "nfc: tag $TAG"
fi

if [[ ! -f "$APP" ]]; then
  echo "nfc: ingen AppImage i dist/. Kør: ./scripts/upload-github.sh --build" >&2
  echo "nfc: koden er alligevel på GitHub: https://github.com/${OWNER_REPO}"
  exit 0
fi

echo "nfc: GitHub Release $TAG ..."
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
tmp="$work/rel.json"
http="$(auth_curl -o "$tmp" -w '%{http_code}' "$API/repos/${OWNER_REPO}/releases/tags/${TAG}" || true)"
if [[ "$http" == "404" ]]; then
  python3 - "$VERSION" "$TAG" <<'PY' > "$work/body.json"
import json, sys
version, tag = sys.argv[1], sys.argv[2]
print(json.dumps({
    "tag_name": tag,
    "name": f"nfc-games {version}",
    "body": "AppImage til Linux x86_64. Gør filen kørbar og kør den én gang.",
    "draft": False,
    "prerelease": False,
}))
PY
  http="$(auth_curl -o "$tmp" -w '%{http_code}' \
    -X POST "$API/repos/${OWNER_REPO}/releases" \
    --data-binary @"$work/body.json")"
fi
if [[ "$http" != "200" && "$http" != "201" ]]; then
  echo "nfc: kunne ikke hente/oprette release (HTTP $http)" >&2
  python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); print(d.get("message", d))' "$tmp" >&2
  exit 1
fi

python3 - "$tmp" "$APP_NAME" "$work" <<'PY'
import json, sys
path, want, work = sys.argv[1], sys.argv[2], sys.argv[3]
d = json.load(open(path))
open(f"{work}/release-id", "w").write(str(d["id"]))
ids = [str(a["id"]) for a in d.get("assets", []) if a.get("name") == want]
open(f"{work}/asset-ids", "w").write("\n".join(ids))
PY
RID="$(cat "$work/release-id")"
if [[ -s "$work/asset-ids" ]]; then
  while read -r aid; do
    [[ -z "$aid" ]] && continue
    auth_curl -o /dev/null -X DELETE "$API/repos/${OWNER_REPO}/releases/assets/${aid}"
  done < "$work/asset-ids"
fi

http="$(curl -sS -o "$tmp" -w '%{http_code}' \
  -H "Authorization: Bearer ${TOKEN}" \
  -H "Accept: application/vnd.github+json" \
  -H "X-GitHub-Api-Version: 2022-11-28" \
  -H "Content-Type: application/octet-stream" \
  --data-binary @"$APP" \
  "https://uploads.github.com/repos/${OWNER_REPO}/releases/${RID}/assets?name=${APP_NAME}")"
if [[ "$http" != "201" ]]; then
  echo "nfc: AppImage-upload fejlede (HTTP $http)" >&2
  python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); print(d.get("message", d))' "$tmp" >&2
  exit 1
fi

echo "nfc: færdig"
echo "    https://github.com/${OWNER_REPO}"
echo "    https://github.com/${OWNER_REPO}/releases/tag/${TAG}"
echo "    https://github.com/${OWNER_REPO}/releases/download/${TAG}/${APP_NAME}"
