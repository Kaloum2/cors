#!/usr/bin/env bash
# Docker entrypoint: CLI sidecar + cors-engine PTY (INT-03).
set -euo pipefail

export CORS_ENGINE_BIN="${CORS_ENGINE_BIN:-/usr/local/bin/cors-engine}"
export CORS_CONF="${CORS_CONF:-/app/conf/cors.conf}"
export CORS_WORKDIR="${CORS_WORKDIR:-/app}"
export CORS_SIDECAR_HOST="${CORS_SIDECAR_HOST:-0.0.0.0}"
export CORS_SIDECAR_PORT="${CORS_SIDECAR_PORT:-7998}"
export PYTHONPATH="/app/scripts:${PYTHONPATH:-}"

exec python3 /app/scripts/cli-sidecar.py \
  --host "$CORS_SIDECAR_HOST" \
  --port "$CORS_SIDECAR_PORT" \
  --engine "$CORS_ENGINE_BIN" \
  --config "$CORS_CONF" \
  --workdir "$CORS_WORKDIR" \
  -t "${CORS_TRACE:-1}"
