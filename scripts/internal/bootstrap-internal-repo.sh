#!/usr/bin/env bash
# Bootstrap private internal repo (Option A) next to the public cors clone.
# Usage: ./scripts/internal/bootstrap-internal-repo.sh git@github.com:ORG/cors-internal.git
set -euo pipefail

INTERNAL_REMOTE="${1:?usage: $0 git@github.com:ORG/cors-internal.git}"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PARENT="$(dirname "$ROOT")"
INTERNAL_DIR="${PARENT}/cors-internal"
PRIVATE_LOCAL="${PARENT}/cors-private-local"
PUBLIC_REMOTE="$(git -C "$ROOT" remote get-url origin 2>/dev/null || echo git@github.com:Kaloum2/cors.git)"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [[ -d "$INTERNAL_DIR/.git" ]]; then
  echo "error: $INTERNAL_DIR already exists (git repo)" >&2
  exit 1
fi

echo "== clone public baseline =="
git clone --branch develop "$PUBLIC_REMOTE" "$INTERNAL_DIR"

echo "== internal gitignore (track private docs/configs) =="
cp "$SCRIPT_DIR/gitignore.internal" "$INTERNAL_DIR/.gitignore"

echo "== remotes: origin=internal, public=upstream =="
git -C "$INTERNAL_DIR" remote rename origin public
git -C "$INTERNAL_DIR" remote add origin "$INTERNAL_REMOTE"

echo "== copy private files from working tree + cors-private-local =="
copy_if() {
  local src="$1" dst="$2"
  if [[ -f "$src" ]]; then
    mkdir -p "$(dirname "$dst")"
    cp -a "$src" "$dst"
  fi
}

for f in doc/PLAN_ACTIONS.md doc/FONCTIONNALITES.md doc/tests_validation.md \
         doc/ARCHITECTURE.md doc/CAHIER_DES_CHARGES.md doc/DECISIONS.md \
         doc/README.md doc/RTN_PLATFORM.md doc/RUNBOOK.md doc/SECURITY.md \
         doc/TESTING.md doc/user_doc.md; do
  copy_if "$ROOT/$f" "$INTERNAL_DIR/$f"
  copy_if "$PRIVATE_LOCAL/$(basename "$f")" "$INTERNAL_DIR/$f"
done

if [[ -d "$ROOT/doc/recap" ]]; then
  cp -a "$ROOT/doc/recap" "$INTERNAL_DIR/doc/"
elif [[ -d "$PRIVATE_LOCAL/recap" ]]; then
  mkdir -p "$INTERNAL_DIR/doc"
  cp -a "$PRIVATE_LOCAL/recap" "$INTERNAL_DIR/doc/"
fi

if [[ -d "$ROOT/doc/dev" ]]; then
  cp -a "$ROOT/doc/dev" "$INTERNAL_DIR/doc/"
fi

mkdir -p "$INTERNAL_DIR/conf/validation"
for f in agentusers-lab cors-centipede.conf ntripsources-centipede; do
  copy_if "$ROOT/conf/validation/$f" "$INTERNAL_DIR/conf/validation/$f"
  copy_if "$PRIVATE_LOCAL/$f" "$INTERNAL_DIR/conf/validation/$f"
done

if [[ -d "$ROOT/test/e2e" ]]; then
  cp -a "$ROOT/test/e2e" "$INTERNAL_DIR/test/"
fi

cp "$SCRIPT_DIR/sync-from-public.sh" "$INTERNAL_DIR/scripts/internal/"
cp "$SCRIPT_DIR/README.md" "$INTERNAL_DIR/scripts/internal/"
chmod +x "$INTERNAL_DIR/scripts/internal/sync-from-public.sh"

echo "== stage internal-only files =="
git -C "$INTERNAL_DIR" add .gitignore scripts/internal/
git -C "$INTERNAL_DIR" add -A doc/ conf/validation/ test/e2e/ 2>/dev/null || true

if git -C "$INTERNAL_DIR" diff --cached --quiet; then
  echo "nothing new to commit (private files missing?)"
else
  git -C "$INTERNAL_DIR" commit -m "$(cat <<'EOF'
chore(internal): bootstrap private docs and lab configs

Track governance docs, validation secrets, and e2e scripts in the
internal repository; public remote stays scrubbed on Kaloum2/cors.
EOF
)"
fi

echo ""
echo "Next steps:"
echo "  1. Create empty private repo on GitHub: cors-internal"
echo "  2. cd $INTERNAL_DIR && git push -u origin develop"
echo "  3. After public updates: ./scripts/internal/sync-from-public.sh"
