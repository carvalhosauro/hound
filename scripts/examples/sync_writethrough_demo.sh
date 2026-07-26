#!/usr/bin/env bash
# Synthetic demo for sync pattern B (write-through upsert + delete).
# No real DB — see docs/sync-writethrough.md.
set -euo pipefail

HOUND_URL="${HOUND_URL:-http://127.0.0.1:8080}"
ID="demo-zed"

echo "health → ${HOUND_URL}/health"
curl -sS -f "${HOUND_URL}/health" >/dev/null

echo "upsert id=${ID}"
curl -sS -f -X POST "${HOUND_URL}/index" \
  -H 'content-type: application/json' \
  -d "{\"id\":\"${ID}\",\"text\":\"Zed Zinc\",\"external_score\":1.5}"
echo

echo "search q=zed"
curl -sS -f "${HOUND_URL}/search?q=zed&limit=3"
echo

echo "delete id=${ID}"
curl -sS -f -X DELETE "${HOUND_URL}/index/${ID}"
echo

echo "search q=zed (expect empty or no ${ID})"
curl -sS -f "${HOUND_URL}/search?q=zed&limit=3"
echo
