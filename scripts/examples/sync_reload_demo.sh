#!/usr/bin/env bash
# Synthetic demo for sync pattern A (full projection push).
# Does NOT talk to a real DB — pushes examples/sample.json via bulk.
# Production A usually restarts with --load; see docs/sync-reload.md.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
HOUND_URL="${HOUND_URL:-http://127.0.0.1:8080}"
SAMPLE="${ROOT}/examples/sample.json"

echo "health → ${HOUND_URL}/health"
curl -sS -f "${HOUND_URL}/health" >/dev/null

echo "bulk load synthetic sample → ${HOUND_URL}/index/bulk"
curl -sS -f -X POST "${HOUND_URL}/index/bulk" \
  -H 'content-type: application/json' \
  --data-binary @"${SAMPLE}"
echo

echo "search"
curl -sS -f "${HOUND_URL}/search?q=ada%20ash&limit=5"
echo
