# Profiling Hound

Use these scripts against **Release** builds **without** sanitizers
(`build-bench/`). Numbers from TSan/ASan builds are not meaningful.

## Tools

| Tool | When to use |
|------|-------------|
| **`perf stat`** | Default on Linux: cycles, instructions, cache refs/misses for a micro filter |
| **`perf record` + FlameGraph** | Hot-path attribution (which functions burn CPU) |
| **Valgrind cachegrind** | No `perf` privileges / want deterministic miss counters (much slower) |
| **Valgrind massif** | Suspected RSS / allocation growth, not latency |

## Prerequisites

```bash
# Fedora
sudo dnf install perf
# Debian/Ubuntu
sudo apt install linux-perf

# Optional FlameGraph scripts (clone once):
git clone https://github.com/brendangregg/FlameGraph.git ~/src/FlameGraph
export FLAMEGRAPH_DIR=~/src/FlameGraph
```

`perf` may require `kernel.perf_event_paranoid` ≤ 1 for non-root use:

```bash
sudo sysctl kernel.perf_event_paranoid=1
```

## Scripts

```bash
# Cache / IPC counters on fuzzy search at 20k
./benchmarks/profiling/perf_stat.sh --benchmark_filter=BM_SearchFuzzy/20000/2

# CPU flamegraph (SVG under benchmarks/results/)
./benchmarks/profiling/flamegraph.sh --benchmark_filter=BM_SearchFuzzy/20000/2
```

Both scripts build `hound_bench_micro` in `build-bench/` if needed.

## Valgrind (manual)

```bash
# Cachegrind (slow)
valgrind --tool=cachegrind ./build-bench/hound_bench_micro \
  --benchmark_filter=BM_SearchExact/1000 --benchmark_min_time=0.1

# Massif heap profile
valgrind --tool=massif ./build-bench/hound_bench_micro \
  --benchmark_filter=BM_Insert/5000 --benchmark_min_time=0.1
ms_print massif.out.* | less
```

Prefer `perf` when available; use Valgrind when you need portable/deterministic
memory or cache accounting and can afford the slowdown.
