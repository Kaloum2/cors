#!/usr/bin/env bash
# Build cors-engine and run unit tests (INT-07).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
cmake "$ROOT"
cmake --build . -j"$JOBS" --target cors-engine cors-mengine test_rtcm_fkp test_dtrignet

echo "== cors-engine =="
"$BUILD_DIR/cors-engine" -h 2>&1 | head -3 || true

if [[ -x "$BUILD_DIR/test/test_rtcm_fkp" ]]; then
  echo "== test_rtcm_fkp =="
  "$BUILD_DIR/test/test_rtcm_fkp"
fi
if [[ -x "$BUILD_DIR/test/test_dtrignet" ]]; then
  echo "== test_dtrignet =="
  "$BUILD_DIR/test/test_dtrignet"
fi

echo "Build OK: $BUILD_DIR/cors-engine"
