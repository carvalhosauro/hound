#!/usr/bin/env bash
# Wrapper → benchmarks/macro/run_macro.sh
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
exec "$ROOT/benchmarks/macro/run_macro.sh" "$@"
