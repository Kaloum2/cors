#!/usr/bin/env bash
# Create GitHub private repo and push cors-internal (requires: gh auth login).
set -euo pipefail

REPO="${1:-Kaloum2/cors-internal}"
INTERNAL_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)/cors-internal"

if ! gh auth status >/dev/null 2>&1; then
  echo "error: run 'gh auth login' first" >&2
  exit 1
fi

if gh repo view "$REPO" >/dev/null 2>&1; then
  echo "repo $REPO already exists"
else
  gh repo create "$REPO" --private --description "CORS Engine internal docs and lab configs"
fi

if [[ ! -d "$INTERNAL_DIR/.git" ]]; then
  echo "error: $INTERNAL_DIR missing — run bootstrap-internal-repo.sh first" >&2
  exit 1
fi

git -C "$INTERNAL_DIR" push -u origin develop
echo "OK: $REPO updated (branch develop)"
