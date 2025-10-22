#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

cleanup() {
  pkill -f '^./controller' 2>/dev/null || true
  fuser -k 3000/tcp 2>/dev/null || true
}

trap cleanup EXIT INT TERM
cleanup

tests=(
  "./test/test-call"
  "./test/test-internal"
  "./test/test-safety"
  "./test/test-car-1"
  "./test/test-car-2"
  "./test/test-car-3"
  "./test/test-car-4"
  "./test/test-car-5"
  "./test/test-controller-1"
  "./test/test-controller-2"
  "./test/test-controller-3"
  "./test/test-controller-4"
)

for t in "${tests[@]}"; do
  echo "--- ${t} ---"
  cleanup
  if ! timeout --preserve-status --signal=TERM --kill-after=2s 12s "$t"; then
    echo "(timed out or failed)"
  fi
  echo
done

cleanup
echo "--- Done ---"
