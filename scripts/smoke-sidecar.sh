#!/usr/bin/env bash
# T1.7 — smoke local sidecar (PING) + optional engine/sidecar if already up.
# Usage:
#   ./scripts/smoke-sidecar.sh              # PING only (expects :7998)
#   ./scripts/smoke-sidecar.sh --with-lab   # start engine+sidecar briefly, PING, stop
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SIDECAR_HOST="${CORS_SIDECAR_HOST:-127.0.0.1}"
SIDECAR_PORT="${CORS_SIDECAR_PORT:-7998}"
ENGINE="${CORS_ENGINE_BIN:-$ROOT/build/cors-engine}"
CONF="${CORS_CONF:-$ROOT/conf/validation/cors-centipede.conf}"
WITH_LAB=0

for arg in "$@"; do
  case "$arg" in
    --with-lab) WITH_LAB=1 ;;
    -h|--help)
      echo "Usage: $0 [--with-lab]"
      exit 0
      ;;
  esac
done

ping_sidecar() {
  local out
  out="$(printf 'PING\n' | nc -w 3 "$SIDECAR_HOST" "$SIDECAR_PORT" 2>/dev/null || true)"
  if echo "$out" | grep -q PONG; then
    echo "OK: sidecar $SIDECAR_HOST:$SIDECAR_PORT -> PONG"
    return 0
  fi
  echo "FAIL: no PONG from $SIDECAR_HOST:$SIDECAR_PORT (got: ${out:-empty})" >&2
  return 1
}

if [[ "$WITH_LAB" -eq 1 ]]; then
  if [[ ! -x "$ENGINE" ]]; then
    echo "FAIL: missing $ENGINE (build cors-engine first)" >&2
    exit 1
  fi
  cleanup() {
    pkill -f "$ENGINE" 2>/dev/null || true
    pkill -f 'cli-sidecar.py' 2>/dev/null || true
  }
  trap cleanup EXIT
  cleanup
  sleep 0.5
  # PTY console for engine (same pattern as ManagedEngine)
  slave="$(python3 - <<'PY'
import os, pty, termios
m,s=pty.openpty()
attrs=termios.tcgetattr(s)
attrs[3]=attrs[3]&~termios.ECHO
termios.tcsetattr(s,termios.TCSANOW,attrs)
print(os.ttyname(s))
os.close(m)
PY
)"
  "$ENGINE" -o "$CONF" -t 0 -d "$slave" -s >/tmp/cors-smoke-engine.log 2>&1 &
  eng_pid=$!
  sleep 4
  python3 "$ROOT/scripts/cli-sidecar.py" --host "$SIDECAR_HOST" --port "$SIDECAR_PORT" \
    >/tmp/cors-smoke-sidecar.log 2>&1 &
  sc_pid=$!
  sleep 2
  ping_sidecar
  kill "$sc_pid" "$eng_pid" 2>/dev/null || true
  wait "$sc_pid" "$eng_pid" 2>/dev/null || true
  trap - EXIT
  cleanup
  exit 0
fi

ping_sidecar
