# Macrobenchmark (HTTP end-to-end)

This suite drives the **real HTTP sidecar** with [hey](https://github.com/rakyll/hey).

## What it measures

Latency percentiles and throughput **including**:

- loopback TCP
- HTTP parse / response write (cpp-httplib)
- JSON serialization of `/search` results
- process scheduling under concurrent clients

It does **not** isolate core fuzzy cost the way `hound_bench_micro` does.
**Do not compare macro milliseconds directly to micro microseconds.**

## Prerequisites

```bash
# Go toolchain required once:
go install github.com/rakyll/hey@latest
# Ensure $(go env GOPATH)/bin is on PATH
```

Release `hound` binary (built by the script into `build-bench/`).

## Run

```bash
./scripts/run_macro.sh
# or:
./benchmarks/macro/run_macro.sh
```

Useful env vars:

| Variable | Default | Meaning |
|----------|---------|---------|
| `HOUND_MACRO_N` | `2000` | Total requests per scenario |
| `HOUND_MACRO_C` | `50` | Concurrent workers |
| `HOUND_MACRO_DOCS` | `5000` | Synthetic docs loaded before load |
| `HOUND_MACRO_PORT` | ephemeral | Bind port |
| `HOUND_HEY_EXTRA` | `-disable-keepalive` | Extra hey flags (keep-alive with httplib can add ~40ms floor) |

Output: `benchmarks/results/macro_<timestamp>.txt` (and a copy under
`baselines/` only when you deliberately promote one).

## Interpreting results

Look at **Latency distribution** lines from hey (`50%`, `90%`, `95%`, `99%`)
and **Requests/sec**.

- p50/p90 for `/search` should be higher than micro fuzzy times (HTTP+JSON).
- Occasional p99 spikes can come from cold connections / scheduling; prefer
  p50–p95 for trend comparison unless spikes are systematic.
- `/health` is the overhead floor for this stack.
- Default runs use `hey -disable-keepalive` because keep-alive + cpp-httplib
  was observed to add an artificial ~40 ms floor in hey’s resp-read timing.
