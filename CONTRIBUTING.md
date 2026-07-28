# Contributing

Short path for humans and agents. Deeper layout, benches, and fuzzy gates:
[`AGENTS.md`](AGENTS.md). Product/DX roadmap: [`docs/DX.md`](docs/DX.md).

## Build & test

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j && ctest --test-dir build --output-on-failure

# Or one script (unit + golden + integration; TSan if libtsan present):
./scripts/run_correctness.sh
```

C++20, CMake. Do **not** run micro/macro benches under sanitizers.

## When to run what

| Change | Run |
|--------|-----|
| Index / HTTP / scoring | `./scripts/run_correctness.sh` |
| Fuzzy, scoring, ingest hot path | `./scripts/run_micro.sh` then `./scripts/compare_bench.py baselines/micro_baseline.json benchmarks/results/micro_*.json` |
| HTTP handlers / load | `./scripts/run_macro.sh` (needs `hey`) |

Default gate: **+10%** `cpu_time` vs the versioned micro baseline. Only refresh
`baselines/micro_baseline.json` when intentionally accepting a regression.

## PR checklist

1. Correctness green for any index/API/scoring touch.
2. Micro compare green (or justify >10% in the PR) for fuzzy/scoring/ingest.
3. If you change HTTP handlers (`include/hound/http_api.hpp`), update
   [`docs/openapi.yaml`](docs/openapi.yaml) in the **same** PR
   ([`docs/compat.md`](docs/compat.md)).
4. Synthetic examples only — no real schemas or PII.
5. Prefer one shippable slice; update `docs/DX.md` Status when a D-id ships.
6. Conventional commits (`feat:`, `fix:`, `docs:`, `ci:`, …).

## Releases

Maintainers: [`docs/release.md`](docs/release.md). Do not force-push `v*` tags.

## Code boundaries

- Core under `include/hound/` stays free of HTTP/CSV wire types where possible
  (`FuzzyIndex` / trie / BK / SymSpell).
- No business domain in core or examples.
- Public HTTP: avoid breaks; see [`docs/compat.md`](docs/compat.md).
