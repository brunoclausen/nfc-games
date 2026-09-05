#!/usr/bin/env bash
# After a green full test: SSH-push to GitHub (deploy key) and upload AppImage (PAT).
# Never logs the token. Never puts it in a git remote URL. Repo is fixed.
set -euo pipefail
set +x

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CONF="${XDG_CONFIG_HOME:-$HOME/.config}/nfc-games"
TOKEN_FILE="$CONF/github.token"
SSH_KEY="$CONF/ssh/github_nfc_games"
GH_REPO="brunoclausen/nfc-games"
APP_NAME="nfc-games-x86_64.AppImage"
VERSION="$(tr -d '[:space:]' < "$ROOT/VERSION")"
TAG="v${VERSION}"

# Do not fall back to GITHUB_TOKEN: Gitea injects its own job token under that name.
TOKEN="${NFC_GITHUB_TOKEN:-}"
if [[ -z "$TOKEN" && -f "$TOKEN_FILE" ]]; then
  TOKEN="$(tr -d '[:space:]' < "$TOKEN_FILE")"
fi
case "$TOKEN" in
  ghp_*|github_pat_*) ;;
  *) TOKEN="" ;;
esac

APP=""
for cand in "$ROOT/dist/$APP_NAME" \
            "${NFC_SRC:-/var/home/bruno/nfc-games}/dist/$APP_NAME"; do
  if [[ -f "$cand" ]]; then
    APP="$cand"
    break
  fi
done
if [[ -z "$APP" ]]; then
  echo "nfc: ingen AppImage at lægge på GitHub" >&2
  exit 1
fi

SRC="$ROOT"
if [[ -d /var/home/bruno/nfc-games/.git ]]; then
  SRC=/var/home/bruno/nfc-games
fi

unset GIT_ASKPASS SSH_ASKPASS DISPLAY || true
export GIT_TERMINAL_PROMPT=0

if [[ ! -f "$SSH_KEY" ]]; then
  echo "nfc: mangler deploy-nøgle $SSH_KEY" >&2
  exit 1
fi
export GIT_SSH_COMMAND="ssh -i ${SSH_KEY} -o IdentitiesOnly=yes -o StrictHostKeyChecking=yes"

echo "nfc: pusher til GitHub ${GH_REPO} via SSH..."
if ! git -C "$SRC" -c credential.helper= push "git@github-nfc-games:${GH_REPO}.git" HEAD:main; then
  echo "nfc: GitHub-upload sprunget over (deploy key mangler endnu)."
  echo "nfc: Full test er OK. Tilføj nøglen her, Allow write access:"
  echo "     https://github.com/${GH_REPO}/settings/keys"
  if [[ -f "${SSH_KEY}.pub" ]]; then
    echo "     $(cat "${SSH_KEY}.pub")"
  fi
  echo "nfc: wiki: http://192.168.1.3:3002/app/nfc-games/wiki/Upload"
  exit 0
fi
# Publisher kun AppImage her. Tagget skubbes IKKE til GitHub:
# release.yml ville ellers bygge + uploade samtidig og overskrive vores testede build.
echo "nfc: kode på https://github.com/${GH_REPO}"

if [[ -z "$TOKEN" ]]; then
  echo "nfc: AppImage ikke lagt på GitHub Release (mangler PAT)."
  echo "nfc: kode er pushet. Til Release: fine-grained token kun til ${GH_REPO}, Contents: Read and write."
  exit 0
fi

work="$(mktemp -d)"
chmod 700 "$work"
trap 'rm -rf "$work"' EXIT
tmp="$work/rel.json"
cfg="$work/curl.cfg"
umask 077
printf 'header = "Authorization: Bearer %s"\n' "$TOKEN" > "$cfg"
printf 'header = "Accept: application/vnd.github+json"\n' >> "$cfg"
printf 'header = "X-GitHub-Api-Version: 2022-11-28"\n' >> "$cfg"
chmod 600 "$cfg"

auth_curl() {
  curl -sS -K "$cfg" "$@"
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
    "body": "AppImage for Linux x86_64. Make it executable and run it once.",
    "draft": False,
    "prerelease": False,
}))
PY
  http="$(auth_curl -o "$tmp" -w '%{http_code}' \
    -H "Content-Type: application/json" \
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
open(f"{work}/release-id", "w").write(str(d["id"]) + "\n")
ids = [str(a["id"]) for a in d.get("assets", []) if a.get("name") == want]
open(f"{work}/asset-ids", "w").write(("\n".join(ids) + "\n") if ids else "")
PY
RID="$(cat "$work/release-id")"
if [[ -s "$work/asset-ids" ]]; then
  while read -r aid; do
    [[ -z "$aid" ]] && continue
    auth_curl -o /dev/null -X DELETE \
      "https://api.github.com/repos/${GH_REPO}/releases/assets/${aid}" || true
  done < "$work/asset-ids"
fi

http="$(auth_curl -o "$tmp" -w '%{http_code}' \
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
