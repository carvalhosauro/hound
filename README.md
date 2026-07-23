# Hound

Lightweight **C++ sidecar** for fuzzy autocomplete + external-score merge.

Hound is **not** a replacement for Elasticsearch/Typesense. It sits beside a
relational database (MySQL/Postgres remain the source of truth), indexes
generic documents `{ id, text, external_score }`, and returns ranked `id`s for
typo-tolerant autocomplete queries.

Domain-agnostic by design: no business schemas, no real-world private data.
Example datasets are synthetic.

## Features (MVP)

- In-memory **Trie** (prefix) + **BK-tree** (Levenshtein fuzzy)
- Configurable merge: `final = alpha * text_relevance + (1-alpha) * norm(external_score)`
- HTTP JSON API
- Bulk load from generic CSV/JSON
- Optional binary snapshot across restarts
- Synthetic benchmarks (latency, Recall@k, ingest, RSS)

## Build

Requirements: CMake ≥ 3.20, a C++20 compiler, Git (FetchContent pulls Catch2,
cpp-httplib, nlohmann/json).

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Binaries:

| Target | Purpose |
|--------|---------|
| `build/hound` | HTTP server |
| `build/hound_bulk_load` | CLI bulk ingest → optional snapshot |
| `build/hound_tests` | unit + golden + integration tests |
| `build/hound_bench` | legacy synthetic summary bench |
| `build-bench/hound_bench_micro` | Google Benchmark micro suite (JSON) |

## Run

```bash
# empty index
./build/hound --host 127.0.0.1 --port 8080

# bulk load + optional snapshot
./build/hound --load examples/sample.csv --snapshot /tmp/hound.snap --port 8080
```

### HTTP API

```bash
curl -s localhost:8080/health

curl -s -X POST localhost:8080/index \
  -H 'content-type: application/json' \
  -d '{"id":"1","text":"Ada Ash","external_score":10}'

curl -s 'localhost:8080/search?q=ada%20ash&limit=5&alpha=0.7'

curl -s -X DELETE localhost:8080/index/1
```

Bulk:

```bash
curl -s -X POST localhost:8080/index/bulk \
  -H 'content-type: application/json' \
  -d '[{"id":"1","text":"Ada Ash","external_score":10},{"id":"2","text":"Blake Brook","external_score":3}]'
```

CSV header for `--load` / `hound_bulk_load`:

```csv
id,text,external_score
1,Ada Ash,10.5
2,Blake Brook,3.0
```

**Security note:** no authentication in the MVP — bind to a trusted network.

## Benchmarks

```bash
# Legacy one-shot summary (p50/p95/p99 + recall@10)
./build/hound_bench

# Micro suite (Google Benchmark → JSON)
./scripts/run_micro.sh
./scripts/save_baseline.sh benchmarks/results/micro_<timestamp>.json

# HTTP macro suite (hey — includes network/JSON overhead)
# Requires: go install github.com/rakyll/hey@latest
./scripts/run_macro.sh
```

Micro reports insert, exact search, fuzzy (edit distance 1–3), and score-merge
latency across index sizes 1k / 5k / 20k (synthetic names, seed 42).

Macro drives concurrent HTTP clients against a live sidecar and writes
`benchmarks/results/macro_<timestamp>.txt`. See
[benchmarks/macro/README.md](benchmarks/macro/README.md).

### Compare & profile

```bash
./scripts/compare_bench.py baselines/micro_baseline.json benchmarks/results/micro_<ts>.json
./benchmarks/profiling/perf_stat.sh --benchmark_filter=BM_SearchFuzzy/20000/2
```

See [AGENTS.md](AGENTS.md) (when/how to run suites) and
[benchmarks/profiling/README.md](benchmarks/profiling/README.md).

## Project layout

See [docs/PLANO.md](docs/PLANO.md) for the phased design,
[docs/REFINEMENT.md](docs/REFINEMENT.md) for post-MVP priorities, and
[AGENTS.md](AGENTS.md) for agent/contributor workflow (tests & benches).

```bash
./scripts/run_correctness.sh          # unit + golden + integration + TSan
HOUND_RUN_TSAN=0 ./scripts/run_correctness.sh   # skip TSan
```

Core headers live under `include/hound/` and have no HTTP/CSV dependencies
except the API/ingest layers.

## License

MIT — see [LICENSE](LICENSE).
