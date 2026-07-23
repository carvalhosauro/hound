#!/usr/bin/env python3
"""Compare two Google Benchmark JSON outputs (baseline vs current).

Exit codes:
  0 — no regression beyond threshold (or only improvements/missing matched)
  1 — one or more benchmarks regressed by more than --threshold
  2 — usage / I/O error
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any


def load_benchmarks(path: Path) -> dict[str, dict[str, Any]]:
    data = json.loads(path.read_text(encoding="utf-8"))
    out: dict[str, dict[str, Any]] = {}
    for b in data.get("benchmarks", []):
        # Prefer aggregate rows when repetitions > 1; otherwise iteration rows.
        name = b.get("run_name") or b.get("name")
        if not name:
            continue
        run_type = b.get("run_type", "iteration")
        if run_type not in ("iteration", "aggregate"):
            continue
        # Keep aggregate if present; else first iteration.
        if name in out and out[name].get("run_type") == "aggregate":
            continue
        if run_type == "aggregate" or name not in out:
            out[name] = b
    return out


def time_seconds(b: dict[str, Any]) -> float | None:
    """Normalize cpu_time (preferred) to seconds using time_unit."""
    if "cpu_time" not in b:
        return None
    t = float(b["cpu_time"])
    unit = b.get("time_unit", "ns")
    scale = {"ns": 1e-9, "us": 1e-6, "ms": 1e-3, "s": 1.0}.get(unit)
    if scale is None:
        return None
    return t * scale


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("baseline", type=Path, help="baseline Google Benchmark JSON")
    ap.add_argument("current", type=Path, help="current Google Benchmark JSON")
    ap.add_argument(
        "--threshold",
        type=float,
        default=0.10,
        help="max allowed relative slowdown (default 0.10 = +10%%)",
    )
    ap.add_argument(
        "--metric",
        choices=("cpu_time", "real_time"),
        default="cpu_time",
        help="which timing field to compare (still normalized via time_unit)",
    )
    args = ap.parse_args()

    if not args.baseline.is_file() or not args.current.is_file():
        print("ERROR: baseline and current must be existing JSON files", file=sys.stderr)
        return 2

    base = load_benchmarks(args.baseline)
    cur = load_benchmarks(args.current)

    def pick_time(b: dict[str, Any]) -> float | None:
        key = args.metric
        if key not in b:
            return time_seconds(b)
        t = float(b[key])
        unit = b.get("time_unit", "ns")
        scale = {"ns": 1e-9, "us": 1e-6, "ms": 1e-3, "s": 1.0}.get(unit)
        return None if scale is None else t * scale

    names = sorted(set(base) & set(cur))
    if not names:
        print("ERROR: no overlapping benchmark names", file=sys.stderr)
        return 2

    print(f"{'benchmark':<32} {'base':>12} {'curr':>12} {'delta%':>10}  verdict")
    print("-" * 78)

    regressions = 0
    for name in names:
        tb = pick_time(base[name])
        tc = pick_time(cur[name])
        if tb is None or tc is None or tb <= 0:
            print(f"{name:<32} {'n/a':>12} {'n/a':>12} {'n/a':>10}  skip")
            continue
        delta = (tc - tb) / tb
        pct = delta * 100.0
        if delta > args.threshold:
            verdict = "REGRESS"
            regressions += 1
        elif delta < -args.threshold:
            verdict = "faster"
        else:
            verdict = "ok"
        print(f"{name:<32} {tb:12.6g} {tc:12.6g} {pct:9.2f}%  {verdict}")

    missing = sorted(set(base) - set(cur))
    extra = sorted(set(cur) - set(base))
    if missing:
        print(f"\nmissing in current ({len(missing)}): {', '.join(missing[:8])}...")
    if extra:
        print(f"extra in current ({len(extra)}): {', '.join(extra[:8])}...")

    print()
    if regressions:
        print(
            f"FAIL: {regressions} benchmark(s) slower than +{args.threshold * 100:.0f}% "
            f"(metric={args.metric})"
        )
        return 1

    print(f"OK: no regression beyond +{args.threshold * 100:.0f}% (metric={args.metric})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
