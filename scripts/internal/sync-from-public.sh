#!/usr/bin/env bash
# Merge latest public develop into the internal repo. Run from cors-internal root.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"

if ! git remote get-url public >/dev/null 2>&1; then
  echo "error: remote 'public' not found (bootstrap first)" >&2
  exit 1
fi

BRANCH="${1:-develop}"

echo "== fetch public =="
git fetch public "$BRANCH"

echo "== merge public/$BRANCH =="
git merge "public/$BRANCH" -m "chore(internal): merge public/$BRANCH"

echo "== push internal =="
git push origin "$BRANCH"

echo "OK: internal branch $BRANCH synced with public"
