#!/usr/bin/env bash
# Commit, push to Gitea (free, always), put AppImage on the Gitea release.
# GitHub is optional if ~/.config/nfc-games/github.token exists.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

CONF="${XDG_CONFIG_HOME:-$HOME/.config}/nfc-games"
TOKEN_FILE="$CONF/github.token"
GH_REPO="${NFC_GITHUB_REPO:-brunoclausen/nfc-games}"
GITEA_REPO="${NFC_GITEA_REPO:-app/nfc-games}"
GITEA_API="${NFC_GITEA_API:-http://192.168.1.3:3002/api/v1}"
APP_NAME="nfc-games-x86_64.AppImage"
APP="$ROOT/dist/$APP_NAME"
NEED_BUILD=0
MSG=""
TOKEN=""

usage() {
  cat <<'EOF'
Brug:
  ./scripts/upload.sh
  ./scripts/upload.sh --build
  ./scripts/upload.sh --build "kort besked om ændringen"

Det lægger ændringen på din Gitea (gratis):
  http://192.168.1.3:3002/app/nfc-games

  --build   byg AppImage først (når du har ændret programmet)

GitHub er valgfrit. Kun hvis du har gemt en nøgle:
  ./scripts/upload.sh --token ghp_DIN_NØGLE
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -h|--help) usage; exit 0 ;;
    --build) NEED_BUILD=1; shift ;;
    --token)
      if [[ -z "${2:-}" ]]; then
        echo "nfc: --token kræver nøglen (ghp_... eller github_pat_...)" >&2
        exit 2
      fi
      case "$2" in
        ghp_*|github_pat_*) ;;
        *)
          echo "nfc: det ligner ikke en GitHub-nøgle" >&2
          exit 2
          ;;
      esac
      mkdir -p "$CONF"
      chmod 700 "$CONF"
      umask 077
      printf '%s\n' "$2" > "$TOKEN_FILE"
      chmod 600 "$TOKEN_FILE"
      echo "nfc: GitHub-nøgle gemt i $TOKEN_FILE (kun denne PC, ikke git)"
      python3 - "$2" <<'PY' || echo "nfc: kunne ikke gemme Gitea-secret (upload virker stadig via filen)" >&2
import json, sys, urllib.request, base64
from pathlib import Path
from urllib.parse import unquote, urlparse
token = sys.argv[1]
cred = Path.home().joinpath(".git-credentials")
url = next((ln.strip() for ln in cred.read_text().splitlines() if "192.168.1.3:3002" in ln), "")
if not url:
    raise SystemExit(1)
p = urlparse(url)
auth = base64.b64encode(f"{unquote(p.username)}:{unquote(p.password)}".encode()).decode()
body = json.dumps({"data": token}).encode()
req = urllib.request.Request(
    "http://192.168.1.3:3002/api/v1/repos/app/nfc-games/actions/secrets/NFC_GITHUB_TOKEN",
    data=body,
    method="PUT",
    headers={
        "Authorization": f"Basic {auth}",
        "Accept": "application/json",
        "Content-Type": "application/json",
    },
)
with urllib.request.urlopen(req, timeout=20) as resp:
    resp.read()
print("nfc: Gitea-secret NFC_GITHUB_TOKEN opdateret")
PY
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

gitea_userpass() {
  python3 - <<'PY'
from pathlib import Path
from urllib.parse import unquote, urlparse
p = Path.home() / ".git-credentials"
if not p.exists():
    raise SystemExit(0)
for ln in p.read_text().splitlines():
    if "192.168.1.3:3002" not in ln:
        continue
    u = urlparse(ln.strip())
    print(unquote(u.username or ""))
    print(unquote(u.password or ""))
    break
PY
}

gitea_curl() {
  curl -sS -u "${GITEA_USER}:${GITEA_PASS}" -H "Accept: application/json" "$@"
}

release_json() {
  python3 - "$1" "$2" <<'PY'
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
}

upload_release() {
  # args: api_base repo auth_header_name auth_header_value upload_url_template
  local api_base="$1" repo="$2" mode="$3" app_path="$4"
  local tmp="$work/rel.json"
  local http rid
  if [[ "$mode" == "gitea" ]]; then
    http="$(gitea_curl -o "$tmp" -w '%{http_code}' "$api_base/repos/${repo}/releases/tags/${TAG}" || true)"
    if [[ "$http" == "404" ]]; then
      release_json "$VERSION" "$TAG" > "$work/body.json"
      http="$(gitea_curl -o "$tmp" -w '%{http_code}' \
        -H "Content-Type: application/json" \
        -X POST "$api_base/repos/${repo}/releases" \
        --data-binary @"$work/body.json")"
    fi
  else
    http="$(auth_curl -o "$tmp" -w '%{http_code}' "$api_base/repos/${repo}/releases/tags/${TAG}" || true)"
    if [[ "$http" == "404" ]]; then
      release_json "$VERSION" "$TAG" > "$work/body.json"
      http="$(auth_curl -o "$tmp" -w '%{http_code}' \
        -X POST "$api_base/repos/${repo}/releases" \
        --data-binary @"$work/body.json")"
    fi
  fi
  if [[ "$http" != "200" && "$http" != "201" ]]; then
    echo "nfc: release ${repo} HTTP $http" >&2
    python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); print(d.get("message", d))' "$tmp" >&2 || true
    return 1
  fi
  python3 - "$tmp" "$APP_NAME" "$work" <<'PY'
import json, sys
path, want, work = sys.argv[1], sys.argv[2], sys.argv[3]
d = json.load(open(path))
open(f"{work}/release-id", "w").write(str(d["id"]))
ids = [str(a["id"]) for a in d.get("assets", []) if a.get("name") == want]
open(f"{work}/asset-ids", "w").write("\n".join(ids))
PY
  rid="$(cat "$work/release-id")"
  if [[ -s "$work/asset-ids" ]]; then
    while read -r aid; do
      [[ -z "$aid" ]] && continue
      if [[ "$mode" == "gitea" ]]; then
        gitea_curl -o /dev/null -X DELETE "$api_base/repos/${repo}/releases/assets/${aid}" || true
      else
        auth_curl -o /dev/null -X DELETE "$api_base/repos/${repo}/releases/assets/${aid}" || true
      fi
    done < "$work/asset-ids"
  fi
  if [[ "$mode" == "gitea" ]]; then
    http="$(gitea_curl -o "$tmp" -w '%{http_code}' \
      -H "Content-Type: application/octet-stream" \
      -X POST "$api_base/repos/${repo}/releases/${rid}/assets?name=${APP_NAME}" \
      --data-binary @"$app_path")"
  else
    http="$(curl -sS -o "$tmp" -w '%{http_code}' \
      -H "Authorization: Bearer ${TOKEN}" \
      -H "Accept: application/vnd.github+json" \
      -H "X-GitHub-Api-Version: 2022-11-28" \
      -H "Content-Type: application/octet-stream" \
      --data-binary @"$app_path" \
      "https://uploads.github.com/repos/${repo}/releases/${rid}/assets?name=${APP_NAME}")"
  fi
  if [[ "$http" != "201" ]]; then
    echo "nfc: AppImage til ${repo} fejlede (HTTP $http)" >&2
    python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); print(d.get("message", d))' "$tmp" >&2 || true
    return 1
  fi
  return 0
}

load_optional_github_token() {
  local raw=""
  if [[ -n "${GITHUB_TOKEN:-}" ]]; then
    raw="$GITHUB_TOKEN"
  elif [[ -n "${GH_TOKEN:-}" ]]; then
    raw="$GH_TOKEN"
  elif [[ -f "$TOKEN_FILE" ]]; then
    raw="$(tr -d '[:space:]' < "$TOKEN_FILE")"
  fi
  if [[ -n "$raw" && "$raw" == ghp_* ]]; then
    TOKEN="$raw"
  else
    TOKEN=""
  fi
}

auth_curl() {
  curl -sS \
    -H "Authorization: Bearer ${TOKEN}" \
    -H "Accept: application/vnd.github+json" \
    -H "X-GitHub-Api-Version: 2022-11-28" \
    "$@"
}

VERSION="$(tr -d '[:space:]' < "$ROOT/VERSION")"
TAG="v${VERSION}"
if [[ -z "$MSG" ]]; then
  MSG="Opdatering ${VERSION} ($(date +%Y-%m-%d))"
fi

mapfile -t _gp < <(gitea_userpass || true)
GITEA_USER="${_gp[0]:-}"
GITEA_PASS="${_gp[1]:-}"
if [[ -z "$GITEA_USER" || -z "$GITEA_PASS" ]]; then
  echo "nfc: mangler Gitea-login i ~/.git-credentials (192.168.1.3:3002)" >&2
  exit 1
fi

load_optional_github_token

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

if ! git rev-parse "$TAG" >/dev/null 2>&1; then
  git tag -a "$TAG" -m "nfc-games ${VERSION}"
fi

echo "nfc: pusher til Gitea..."
unset GIT_ASKPASS SSH_ASKPASS DISPLAY || true
export GIT_TERMINAL_PROMPT=0
git push origin HEAD:main
git push origin "$TAG" || true

if [[ -n "$TOKEN" ]]; then
  echo "nfc: pusher også til GitHub..."
  if ! git remote get-url github >/dev/null 2>&1; then
    git remote add github "https://github.com/${GH_REPO}.git"
  fi
  git -c credential.helper= push \
    "https://x-access-token:${TOKEN}@github.com/${GH_REPO}.git" HEAD:main || \
    echo "nfc: GitHub-push sprunget over" >&2
  git -c credential.helper= push \
    "https://x-access-token:${TOKEN}@github.com/${GH_REPO}.git" "$TAG" || true
fi

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

if [[ ! -f "$APP" ]]; then
  echo "nfc: ingen AppImage i dist/. Kør: ./scripts/upload.sh --build" >&2
  echo "nfc: koden er på Gitea: http://192.168.1.3:3002/${GITEA_REPO}"
  exit 0
fi

echo "nfc: Gitea Release $TAG ..."
upload_release "$GITEA_API" "$GITEA_REPO" gitea "$APP"
echo "nfc: Gitea: http://192.168.1.3:3002/${GITEA_REPO}/releases/tag/${TAG}"

if [[ -n "$TOKEN" ]]; then
  echo "nfc: GitHub Release $TAG ..."
  if upload_release "https://api.github.com" "$GH_REPO" github "$APP"; then
    echo "nfc: GitHub: https://github.com/${GH_REPO}/releases/tag/${TAG}"
  fi
fi

echo "nfc: færdig (uden GitHub-betaling)"
echo "    kode:     http://192.168.1.3:3002/${GITEA_REPO}"
echo "    actions:  http://192.168.1.3:3002/${GITEA_REPO}/actions"
echo "    appimage: http://192.168.1.3:3002/${GITEA_REPO}/releases/tag/${TAG}"
