#!/usr/bin/env bash
# Promote a micro JSON result to the versioned baseline.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="${1:-}"
DEST="$ROOT/baselines/micro_baseline.json"

if [[ -z "$SRC" ]]; then
  echo "Usage: $0 <path-to-micro.json>"
  echo "Example: $0 benchmarks/results/micro_....json"
  exit 2
fi

mkdir -p "$(dirname "$DEST")"
cp "$SRC" "$DEST"
echo "saved baseline → $DEST"
