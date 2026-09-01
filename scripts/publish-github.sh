#!/usr/bin/env bash
# After a green full test: push main/tag to GitHub and replace the AppImage on the Release.
# Token: env GITHUB_TOKEN / NFC_GITHUB_TOKEN, or ~/.config/nfc-games/github.token
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CONF="${XDG_CONFIG_HOME:-$HOME/.config}/nfc-games"
TOKEN_FILE="$CONF/github.token"
GH_REPO="${NFC_GITHUB_REPO:-brunoclausen/nfc-games}"
APP_NAME="nfc-games-x86_64.AppImage"
VERSION="$(tr -d '[:space:]' < "$ROOT/VERSION")"
TAG="v${VERSION}"

TOKEN="${NFC_GITHUB_TOKEN:-${GITHUB_TOKEN:-${GH_TOKEN:-}}}"
if [[ -z "$TOKEN" && -f "$TOKEN_FILE" ]]; then
  TOKEN="$(tr -d '[:space:]' < "$TOKEN_FILE")"
fi
if [[ -z "$TOKEN" || ( "$TOKEN" != ghp_* && "$TOKEN" != github_pat_* ) ]]; then
  echo "nfc: GitHub-upload sprunget over (ingen GitHub-nøgle)."
  echo "nfc: gem en classic token (kun repo) med:"
  echo "     ./scripts/upload.sh --token ghp_DIN_NØGLE"
  exit 0
fi

APP=""
for cand in "$ROOT/dist/$APP_NAME" \
            "${NFC_SRC:-/var/home/bruno/nfc-games}/dist/$APP_NAME"; do
  if [[ -f "$cand" ]]; then
    APP="$cand"
    break
  fi
done
if [[ -z "$APP" ]]; then
  echo "nfc: ingen AppImage at lægge på GitHub (kør full-test eller build-appimage først)" >&2
  exit 1
fi

SRC="$ROOT"
if [[ -d /var/home/bruno/nfc-games/.git ]]; then
  SRC=/var/home/bruno/nfc-games
fi

unset GIT_ASKPASS SSH_ASKPASS DISPLAY || true
export GIT_TERMINAL_PROMPT=0
AUTH_URL="https://x-access-token:${TOKEN}@github.com/${GH_REPO}.git"

echo "nfc: pusher ${SRC} → GitHub ${GH_REPO} ..."
git -C "$SRC" -c credential.helper= push "$AUTH_URL" HEAD:main
if git -C "$SRC" rev-parse "$TAG" >/dev/null 2>&1; then
  git -C "$SRC" -c credential.helper= push "$AUTH_URL" "$TAG" || true
fi
echo "nfc: kode på https://github.com/${GH_REPO}"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
tmp="$work/rel.json"

auth_curl() {
  curl -sS \
    -H "Authorization: Bearer ${TOKEN}" \
    -H "Accept: application/vnd.github+json" \
    -H "X-GitHub-Api-Version: 2022-11-28" \
    "$@"
}

http="$(auth_curl -o "$tmp" -w '%{http_code}' \
  "https://api.github.com/repos/${GH_REPO}/releases/tags/${TAG}" || true)"
if [[ "$http" == "404" ]]; then
  python3 - "$VERSION" "$TAG" <<'PY' > "$work/body.json"
import json, sys
version, tag = sys.argv[1], sys.argv[2]
print(json.dumps({
    "tag_name": tag,
    "name": f"nfc-games {version}",
    "body": "AppImage for Linux x86_64. Make it executable and run it once.\n\nBuilt by Gitea full test, then published here.",
    "draft": False,
    "prerelease": False,
}))
PY
  http="$(auth_curl -o "$tmp" -w '%{http_code}' \
    -X POST "https://api.github.com/repos/${GH_REPO}/releases" \
    --data-binary @"$work/body.json")"
fi
if [[ "$http" != "200" && "$http" != "201" ]]; then
  echo "nfc: GitHub release HTTP $http" >&2
  python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); print(d.get("message", d))' "$tmp" >&2 || true
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
    auth_curl -o /dev/null -X DELETE \
      "https://api.github.com/repos/${GH_REPO}/releases/assets/${aid}" || true
  done < "$work/asset-ids"
fi

http="$(curl -sS -o "$tmp" -w '%{http_code}' \
  -H "Authorization: Bearer ${TOKEN}" \
  -H "Accept: application/vnd.github+json" \
  -H "X-GitHub-Api-Version: 2022-11-28" \
  -H "Content-Type: application/octet-stream" \
  --data-binary @"$APP" \
  "https://uploads.github.com/repos/${GH_REPO}/releases/${RID}/assets?name=${APP_NAME}")"
if [[ "$http" != "201" ]]; then
  echo "nfc: GitHub AppImage-upload HTTP $http" >&2
  python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); print(d.get("message", d))' "$tmp" >&2 || true
  exit 1
fi

echo "nfc: GitHub Release https://github.com/${GH_REPO}/releases/tag/${TAG}"
echo "nfc: AppImage https://github.com/${GH_REPO}/releases/download/${TAG}/${APP_NAME}"
