#!/usr/bin/env bash
# Back-compat: same as ./scripts/upload.sh (Gitea first, GitHub optional).
exec "$(cd "$(dirname "$0")" && pwd)/upload.sh" "$@"
