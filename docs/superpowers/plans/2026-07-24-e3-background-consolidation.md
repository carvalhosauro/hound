# E3 Background Consolidation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Opt-in time-based background consolidation on top of E2 publish-swap so live upserts amortize SymSpell `prepare`+publish across a `--consolidate-ms N` interval, recovering mixed write throughput while search still reads an atomic snapshot.

**Architecture:** When `PublishMode::PublishSwap` and `consolidate_ms > 0`, upserts/erases mutate `draft_` under `writer_mu_`, set `dirty_`, and skip sync publish. A `std::jthread` wakes every N ms and, if dirty and not bulk-deferred, runs `prepare` + atomic publish. `prepare()` remains a sync flush; `clear` publishes immediately. `consolidate_ms == 0` keeps E2 sync publish-swap. Legacy `shared_mutex` unchanged.

**Tech Stack:** C++20 (`std::jthread`, `std::stop_token`, `std::atomic<std::shared_ptr<T>>`), Catch2, existing Release `hound` + hey probe under `scripts/tmp/`.

**Spec:** `docs/superpowers/specs/2026-07-24-e3-background-consolidation-design.md`

## Global Constraints

- Default concurrency remains `shared_mutex` (`PublishMode::Legacy`); do **not** flip `--publish-swap` default.
- E3 requires publish-swap: `--consolidate-ms N` / `HOUND_CONSOLIDATE_MS=N` with `N > 0` without publish-swap → CLI error + usage.
- `N == 0` or omit = E2 sync publish-swap when `--publish-swap` is set.
- Search/`get`/`size`/`copy_documents` always read `published_` (eventual lag).
- Explicit `prepare()` sync-flushes; no new HTTP `/flush` endpoint.
- Micro `compare_bench.py` gate applies to **legacy/default** path vs `baselines/micro_baseline.json`.
- `./scripts/run_correctness.sh` must pass (includes TSan when libtsan present).
- Probe scripts stay under `scripts/tmp/` (gitignored); do not commit them.
- Core headers stay HTTP/CSV-free.
- Conventional commits; English docs/comments.

## File structure

| Path | Role |
|------|------|
| `include/hound/fuzzy_index.hpp` | `consolidate_ms`, dirty flag, `jthread` worker, deferred publish |
| `src/main.cpp` | `--consolidate-ms` / env parse; reject without publish-swap |
| `README.md` | One-liner for E3 flag |
| `tests/unit/test_fuzzy_index.cpp` | Deferred visibility, flush, clear, interval accessor |
| `tests/concurrency/test_tsan_rw.cpp` | Concurrent search+upsert under E3 |
| `scripts/tmp/probe_e3_mixed_load.sh` | E2 vs E3 probe (untracked; may wrap E2 probe) |
| `docs/REFINEMENT.md` | Status + Phase 2 changelog |
| `docs/superpowers/specs/2026-07-24-e3-background-consolidation-design.md` | Spec (already written) |

---

### Task 1: Defer publish when `consolidate_ms > 0` (no worker yet)

**Files:**
- Modify: `include/hound/fuzzy_index.hpp`
- Test: `tests/unit/test_fuzzy_index.cpp`

**Interfaces:**
- Consumes: existing `PublishMode::PublishSwap`, `IndexState`, `published_`, `draft_`, `writer_mu_`, `defer_publish_`
- Produces:
  - `FuzzyIndex(..., PublishMode mode = Legacy, std::chrono::milliseconds consolidate_ms = 0)`
  - `std::chrono::milliseconds consolidate_ms() const`
  - `inline std::chrono::milliseconds consolidate_ms_from_env()`
  - When `mode == PublishSwap && consolidate_ms > 0`: upsert/erase set dirty and **do not** publish; `prepare()` publishes; `clear` still publishes immediately; `consolidate_ms == 0` keeps E2 sync publish

- [ ] **Step 1: Write the failing tests**

Append to `tests/unit/test_fuzzy_index.cpp` (add `#include <chrono>` if missing):

```cpp
TEST_CASE("e3 deferred upsert not visible until prepare", "[fuzzy_index][e3]") {
  using namespace std::chrono_literals;
  hound::FuzzyIndex idx(hound::make_default_fuzzy_backend(), hound::make_default_ranker(),
                        hound::PublishMode::PublishSwap, 500ms);
  REQUIRE(idx.consolidate_ms() == 500ms);
  idx.upsert({"1", "Alpha Ridge", 10.0});
  auto before = idx.search("alpha ridge", {.limit = 5});
  REQUIRE(before.empty());
  REQUIRE_FALSE(idx.get("1").has_value());
  REQUIRE(idx.size() == 0);
  idx.prepare();
  auto after = idx.search("alpha ridge", {.limit = 5});
  REQUIRE_FALSE(after.empty());
  REQUIRE(after.front().id == "1");
  REQUIRE(idx.get("1").has_value());
  REQUIRE(idx.size() == 1);
}

TEST_CASE("e3 clear publishes immediately", "[fuzzy_index][e3]") {
  using namespace std::chrono_literals;
  hound::FuzzyIndex idx(hound::make_default_fuzzy_backend(), hound::make_default_ranker(),
                        hound::PublishMode::PublishSwap, 500ms);
  idx.upsert({"1", "Alpha Ridge", 10.0});
  idx.prepare();
  REQUIRE(idx.size() == 1);
  idx.clear();
  REQUIRE(idx.size() == 0);
  REQUIRE(idx.search("alpha ridge", {.limit = 5}).empty());
}

TEST_CASE("e3 consolidate_ms zero keeps sync publish-swap", "[fuzzy_index][e3]") {
  hound::FuzzyIndex idx(hound::make_default_fuzzy_backend(), hound::make_default_ranker(),
                        hound::PublishMode::PublishSwap, std::chrono::milliseconds{0});
  idx.upsert({"1", "Alpha Ridge", 10.0});
  auto hits = idx.search("alpha ridge", {.limit = 5});
  REQUIRE_FALSE(hits.empty());
  REQUIRE(hits.front().id == "1");
}
```

- [ ] **Step 2: Run tests to verify they fail**

```bash
cmake --build build -j"$(nproc)" --target hound_tests
./build/hound_tests "[e3]"
```

Expected: FAIL to compile (`consolidate_ms` ctor / accessor unknown) or FAIL assertions.

- [ ] **Step 3: Extend ctor, env helper, and defer logic (still no worker)**

In `include/hound/fuzzy_index.hpp`, add includes:

```cpp
#include <chrono>
#include <condition_variable>
#include <thread>
```

After `publish_mode_from_env()`, add:

```cpp
inline std::chrono::milliseconds consolidate_ms_from_env() {
  const char* raw = std::getenv("HOUND_CONSOLIDATE_MS");
  if (raw == nullptr || raw[0] == '\0') {
    return std::chrono::milliseconds{0};
  }
  char* end = nullptr;
  const unsigned long v = std::strtoul(raw, &end, 10);
  if (end == raw || (end != nullptr && *end != '\0')) {
    return std::chrono::milliseconds{0};
  }
  return std::chrono::milliseconds{v};
}
```

Change ctor signature and init list:

```cpp
  explicit FuzzyIndex(std::unique_ptr<FuzzyBackend> fuzzy = make_default_fuzzy_backend(),
                      std::unique_ptr<Ranker> ranker = make_default_ranker(),
                      PublishMode mode = PublishMode::Legacy,
                      std::chrono::milliseconds consolidate_ms = std::chrono::milliseconds{0})
      : mode_(mode),
        consolidate_ms_(mode == PublishMode::PublishSwap ? consolidate_ms
                                                         : std::chrono::milliseconds{0}),
        ranker_(std::move(ranker)),
        state_(std::move(fuzzy)) {
    if (!ranker_) {
      ranker_ = make_default_ranker();
    }
    if (mode_ == PublishMode::PublishSwap) {
      draft_ = IndexState(state_.fuzzy ? state_.fuzzy->clone() : make_default_fuzzy_backend());
      published_.store(std::make_shared<const IndexState>(draft_), std::memory_order_release);
    }
  }
```

Add public accessor:

```cpp
  std::chrono::milliseconds consolidate_ms() const { return consolidate_ms_; }
```

Add private helper (used by prepare/clear/upsert path):

```cpp
  void publish_draft_unlocked() {
    draft_.fuzzy->prepare();
    published_.store(std::make_shared<const IndexState>(draft_), std::memory_order_release);
    dirty_ = false;
  }

  bool deferred_publish_active() const {
    return mode_ == PublishMode::PublishSwap && consolidate_ms_.count() > 0;
  }
```

Update `upsert` PublishSwap branch:

```cpp
    if (mode_ == PublishMode::PublishSwap) {
      std::lock_guard wlock(writer_mu_);
      apply_upsert(draft_, std::move(doc), normalized);
      if (!defer_publish_) {
        if (deferred_publish_active()) {
          dirty_ = true;
        } else {
          publish_draft_unlocked();
        }
      } else if (deferred_publish_active()) {
        dirty_ = true;
      }
      return;
    }
```

Same pattern for `erase` (only publish/dirty when erase succeeded).

Update `clear` PublishSwap branch to always `publish_draft_unlocked()` after clearing draft (never leave empty delayed).

Update `prepare()` PublishSwap branch:

```cpp
      draft_.fuzzy->prepare();
      published_.store(std::make_shared<const IndexState>(draft_), std::memory_order_release);
      dirty_ = false;
      defer_publish_ = false;
```

(Or call `publish_draft_unlocked()` then `defer_publish_ = false`.)

Add members (before legacy/`PublishSwap` fields is fine; keep `worker_` for Task 2 last):

```cpp
  std::chrono::milliseconds consolidate_ms_{0};
  bool dirty_ = false;
```

- [ ] **Step 4: Run tests to verify they pass**

```bash
cmake --build build -j"$(nproc)" --target hound_tests
./build/hound_tests "[e3],[publish_swap]"
```

Expected: PASS (existing publish-swap + new e3 cases).

- [ ] **Step 5: Commit**

```bash
git add include/hound/fuzzy_index.hpp tests/unit/test_fuzzy_index.cpp
git commit -m "$(cat <<'EOF'
feat(core): defer publish-swap when consolidate_ms > 0

E3 step: upsert/erase mark dirty without sync prepare; prepare()/clear still publish.
EOF
)"
```

---

### Task 2: Background `jthread` consolidator

**Files:**
- Modify: `include/hound/fuzzy_index.hpp`
- Test: `tests/unit/test_fuzzy_index.cpp`

**Interfaces:**
- Consumes: Task 1 `dirty_`, `deferred_publish_active()`, `publish_draft_unlocked()`, `defer_publish_`
- Produces: background worker that every `consolidate_ms_` (if dirty && !defer) publishes; dtor stops/joins cleanly; `prepare()` notifies worker

- [ ] **Step 1: Write the failing visibility-after-tick test**

```cpp
TEST_CASE("e3 worker publishes dirty draft after interval", "[fuzzy_index][e3]") {
  using namespace std::chrono_literals;
  hound::FuzzyIndex idx(hound::make_default_fuzzy_backend(), hound::make_default_ranker(),
                        hound::PublishMode::PublishSwap, 50ms);
  idx.upsert({"1", "Alpha Ridge", 10.0});
  REQUIRE(idx.search("alpha ridge", {.limit = 5}).empty());
  // Wait well past one consolidate interval (+ prepare slack).
  std::this_thread::sleep_for(300ms);
  auto hits = idx.search("alpha ridge", {.limit = 5});
  REQUIRE_FALSE(hits.empty());
  REQUIRE(hits.front().id == "1");
}

TEST_CASE("e3 begin_bulk defers worker until prepare", "[fuzzy_index][e3]") {
  using namespace std::chrono_literals;
  hound::FuzzyIndex idx(hound::make_default_fuzzy_backend(), hound::make_default_ranker(),
                        hound::PublishMode::PublishSwap, 50ms);
  idx.begin_bulk();
  idx.upsert({"1", "Alpha Ridge", 10.0});
  std::this_thread::sleep_for(300ms);
  REQUIRE(idx.search("alpha ridge", {.limit = 5}).empty());
  idx.prepare();
  REQUIRE_FALSE(idx.search("alpha ridge", {.limit = 5}).empty());
}
```

Ensure `#include <thread>` is present in the test file.

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build -j"$(nproc)" --target hound_tests
./build/hound_tests "e3 worker publishes"
```

Expected: FAIL — search still empty after sleep (no worker yet).

- [ ] **Step 3: Start `jthread` worker in ctor; stop in dtor**

Declare members so `worker_` is **last** (destroyed first while other state still alive):

```cpp
  std::condition_variable cv_;
  std::jthread worker_;
```

Add private:

```cpp
  void start_consolidator_if_needed() {
    if (!deferred_publish_active()) {
      return;
    }
    worker_ = std::jthread([this](std::stop_token st) {
      std::stop_callback on_stop(st, [this] { cv_.notify_all(); });
      std::unique_lock lock(writer_mu_);
      while (!st.stop_requested()) {
        cv_.wait_for(lock, consolidate_ms_, [&] { return st.stop_requested(); });
        if (st.stop_requested()) {
          break;
        }
        if (dirty_ && !defer_publish_) {
          publish_draft_unlocked();
        }
      }
    });
  }

  void stop_consolidator() {
    if (worker_.joinable()) {
      worker_.request_stop();
      cv_.notify_all();
      // jthread dtor also joins; explicit reset for clear ownership in ~FuzzyIndex
      worker_ = std::jthread{};
    }
  }
```

At end of ctor (after initializing `published_`):

```cpp
    start_consolidator_if_needed();
```

Add destructor (public):

```cpp
  ~FuzzyIndex() { stop_consolidator(); }
```

FuzzyIndex is currently implicitly movable/non-copyable due to mutex/atomic — fine. If any test copied it, they already cannot.

In `prepare()` after publishing, `cv_.notify_all()` is optional (sync path already flushed).

In `begin_bulk()`, keep setting `defer_publish_ = true` under `writer_mu_`.

- [ ] **Step 4: Run e3 + publish_swap tests**

```bash
cmake --build build -j"$(nproc)" --target hound_tests
./build/hound_tests "[e3],[publish_swap]"
```

Expected: PASS. If flaky, increase sleep to `500ms` or interval to `20ms` with sleep `250ms` — prefer longer sleep over shorter interval for CI noise.

- [ ] **Step 5: Commit**

```bash
git add include/hound/fuzzy_index.hpp tests/unit/test_fuzzy_index.cpp
git commit -m "$(cat <<'EOF'
feat(core): background consolidate dirty publish-swap drafts

Sonic-style jthread publishes at consolidate_ms when dirty; begin_bulk still defers.
EOF
)"
```

---

### Task 3: CLI / env wire + README

**Files:**
- Modify: `src/main.cpp`
- Modify: `README.md`
- Optional smoke: shell against `./build/hound --help` and error path (no Catch harness for CLI today)

**Interfaces:**
- Consumes: `hound::consolidate_ms_from_env()`, `PublishMode`, `FuzzyIndex` 4-arg ctor
- Produces: `--consolidate-ms N`; env `HOUND_CONSOLIDATE_MS`; error if N>0 without publish-swap; log line for consolidate ms

- [ ] **Step 1: Extend `usage()` and parsing**

In `src/main.cpp` `usage()`, add under `--publish-swap` lines:

```text
      << "       [--publish-swap] [--consolidate-ms MS]\n"
```

and help text:

```text
      << "  --consolidate-ms  E3: background publish interval in ms (requires --publish-swap);\n"
      << "                    0/omit = publish each write (E2). Also HOUND_CONSOLIDATE_MS\n";
```

In `main`, after `publish_mode` init:

```cpp
  auto consolidate_ms = hound::consolidate_ms_from_env();
```

In the arg loop, after `--publish-swap`:

```cpp
    } else if (arg == "--consolidate-ms") {
      const std::string value = need("--consolidate-ms");
      char* end = nullptr;
      const unsigned long v = std::strtoul(value.c_str(), &end, 10);
      if (end == value.c_str() || (end != nullptr && *end != '\0')) {
        std::cerr << "invalid --consolidate-ms: " << value << " (unsigned ms)\n";
        return 2;
      }
      consolidate_ms = std::chrono::milliseconds{v};
```

Need `#include <chrono>` in `main.cpp`.

After the arg loop, before constructing the index:

```cpp
  if (consolidate_ms.count() > 0 && publish_mode != hound::PublishMode::PublishSwap) {
    std::cerr << "--consolidate-ms requires --publish-swap (or HOUND_PUBLISH_SWAP=1)\n";
    usage(argv[0]);
    return 2;
  }
```

Construct:

```cpp
  hound::FuzzyIndex index(hound::make_fuzzy_backend(fuzzy_kind),
                          hound::make_ranker(ranker_kind), publish_mode, consolidate_ms);
```

Log after publish mode line:

```cpp
  if (publish_mode == hound::PublishMode::PublishSwap) {
    std::cerr << "consolidate_ms: " << consolidate_ms.count() << "\n";
  }
```

- [ ] **Step 2: README one-liner**

In `README.md` concurrency section, after the publish-swap paragraph, add:

```markdown
With publish-swap, optional `--consolidate-ms N` / `HOUND_CONSOLIDATE_MS=N` (E3) batches
publishes on a background timer so writes stay cheap; search may lag by about N ms until
the next consolidate (or until `prepare()` / bulk finish).
```

Example command:

```bash
./build/hound --publish-swap --consolidate-ms 200 --load examples/sample.csv --port 8080
```

- [ ] **Step 3: Manual CLI smoke**

```bash
cmake --build build -j"$(nproc)" --target hound
./build/hound --help | grep -q consolidate-ms
./build/hound --consolidate-ms 200 --port 1; test $? -eq 2
```

Expected: help shows flag; second command exits 2 with requires `--publish-swap` message (port unused).

- [ ] **Step 4: Commit**

```bash
git add src/main.cpp README.md
git commit -m "$(cat <<'EOF'
feat(cli): wire --consolidate-ms and HOUND_CONSOLIDATE_MS

Requires --publish-swap; 0/omit keeps E2 sync publish-per-write.
EOF
)"
```

---

### Task 4: TSan coverage for E3

**Files:**
- Modify: `tests/concurrency/test_tsan_rw.cpp`

**Interfaces:**
- Consumes: `FuzzyIndex(..., PublishMode::PublishSwap, 50ms)`
- Produces: Catch case `[tsan][concurrency][e3]` concurrent search+upsert

- [ ] **Step 1: Add TSan test**

Append (mirror publish-swap case; shorter interval so worker runs during the 800 ms window):

```cpp
TEST_CASE("concurrent search with upsert under TSan (e3 consolidate)",
          "[tsan][concurrency][e3]") {
  using namespace std::chrono_literals;
  hound::FuzzyIndex index(hound::make_default_fuzzy_backend(), hound::make_default_ranker(),
                          hound::PublishMode::PublishSwap, 50ms);
  for (int i = 0; i < 50; ++i) {
    index.upsert(
        {"seed-" + std::to_string(i), "Seed Name " + std::to_string(i), static_cast<double>(i)});
  }
  index.prepare();  // publish seeds before concurrent load

  std::atomic<bool> stop{false};
  std::atomic<std::uint64_t> searches{0};
  std::atomic<std::uint64_t> upserts{0};

  auto searcher = [&] {
    while (!stop.load(std::memory_order_relaxed)) {
      auto hits = index.search("seed name", {.limit = 10, .max_edit_distance = 2});
      (void)hits;
      searches.fetch_add(1, std::memory_order_relaxed);
    }
  };

  auto writer = [&] {
    int i = 1000;
    while (!stop.load(std::memory_order_relaxed)) {
      const std::string id = "w-" + std::to_string(i);
      index.upsert({id, "Writer Name " + std::to_string(i), 1.0});
      if (i % 3 == 0) {
        index.erase(id);
      }
      ++i;
      upserts.fetch_add(1, std::memory_order_relaxed);
    }
  };

  std::vector<std::thread> threads;
  threads.emplace_back(searcher);
  threads.emplace_back(searcher);
  threads.emplace_back(writer);

  std::this_thread::sleep_for(std::chrono::milliseconds(800));
  stop.store(true, std::memory_order_relaxed);
  for (auto& t : threads) {
    t.join();
  }

  index.prepare();  // flush remaining dirty before size check
  REQUIRE(searches.load() > 0);
  REQUIRE(upserts.load() > 0);
  REQUIRE(index.size() >= 50);
}
```

Add `#include <chrono>` if not already present.

- [ ] **Step 2: Run correctness (includes TSan when available)**

```bash
./scripts/run_correctness.sh
```

Expected: PASS (unit + golden + integration + TSan e3 when libtsan present).

- [ ] **Step 3: Commit**

```bash
git add tests/concurrency/test_tsan_rw.cpp
git commit -m "$(cat <<'EOF'
test(tsan): exercise e3 consolidate under concurrent R/W

EOF
)"
```

---

### Task 5: Probe E2 vs E3 + REFINEMENT changelog

**Files:**
- Create (untracked): `scripts/tmp/probe_e3_mixed_load.sh` (thin wrapper around E2 probe args)
- Modify (tracked): `docs/REFINEMENT.md`
- Optional: legacy micro gate if Task 1–2 touched default path layout

**Interfaces:**
- Consumes: Release `hound` with `--publish-swap` and `--publish-swap --consolidate-ms N`
- Produces: probe artifacts; changelog with writes/s + p99; status E3 done

- [ ] **Step 1: Write untracked probe wrapper**

`scripts/tmp/probe_e3_mixed_load.sh`:

```bash
#!/usr/bin/env bash
# E3 mixed-load probe — untracked. Do not commit.
# Compares E2 sync publish-swap vs E3 consolidate. Reuses probe_e2_mixed_load.sh.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
E2="$ROOT/scripts/tmp/probe_e2_mixed_load.sh"
N_MS="${HOUND_E3_CONSOLIDATE_MS:-200}"
OUT_DIR="${HOUND_BENCH_OUT_DIR:-$ROOT/benchmarks/results}"
mkdir -p "$OUT_DIR"

if [[ ! -x "$E2" && ! -f "$E2" ]]; then
  echo "ERROR: missing $E2 (copy from E2 session or recreate)"
  exit 1
fi

echo "=== E2 publish-swap (sync) ==="
HOUND_E2_LABEL=swap_sync \
  HOUND_E2_HOUND_ARGS=--publish-swap \
  "$E2" "$OUT_DIR/e3_before_e2_sync_$(date -u +%Y%m%dT%H%M%SZ).txt"

echo "=== E3 publish-swap + consolidate-ms=${N_MS} ==="
HOUND_E2_LABEL=swap_e3 \
  HOUND_E2_HOUND_ARGS="--publish-swap --consolidate-ms ${N_MS}" \
  "$E2" "$OUT_DIR/e3_after_consolidate_${N_MS}ms_$(date -u +%Y%m%dT%H%M%SZ).txt"
```

`chmod +x` the script. Do **not** `git add` it.

- [ ] **Step 2: Ensure Release binary exists**

```bash
cmake -S . -B build-bench -DCMAKE_BUILD_TYPE=Release \
  -DHOUND_BUILD_BENCH=ON -DHOUND_ENABLE_TSAN=OFF
cmake --build build-bench -j"$(nproc)" --target hound
```

- [ ] **Step 3: Run probe**

```bash
HOUND_E3_CONSOLIDATE_MS=200 ./scripts/tmp/probe_e3_mixed_load.sh
```

Record mixed writes/s and exact/typo p99 for E2 sync vs E3. If writes/s barely move, retry `HOUND_E3_CONSOLIDATE_MS=500`.

- [ ] **Step 4: Optional micro gate (legacy default)**

```bash
./scripts/run_micro.sh benchmarks/results/micro_e3_legacy.json
./scripts/compare_bench.py baselines/micro_baseline.json benchmarks/results/micro_e3_legacy.json
```

Expected: pass (default path unchanged).

- [ ] **Step 5: Update `docs/REFINEMENT.md`**

- Status block: paused after **E3** (or iterate); table row for E3 commit(s).
- Phase E table: E3 **Done** with writes/s + interval N.
- Learning map Sonic row: periodic consolidation **Applied (E3 opt-in)**.
- “Not started” / suggested next: remove E3; point to F/H/#2–#5.
- Phase 2 changelog entry using spec template with real artifact paths and numbers.
- Decision: ship opt-in; **do not** flip defaults.

- [ ] **Step 6: Commit docs only**

```bash
git add docs/REFINEMENT.md
git status  # confirm scripts/tmp not staged
git commit -m "$(cat <<'EOF'
docs: record Phase E3 background consolidation results

EOF
)"
```

---

## Self-review (plan vs spec)

| Spec requirement | Task |
|------------------|------|
| Time-based background consolidation | Task 2 |
| Only on top of publish-swap | Tasks 1–3 (ctor clamps; CLI rejects) |
| Eventual visibility + `prepare()` flush | Task 1 |
| Explicit `--consolidate-ms` / env; 0 = E2 | Tasks 1, 3 |
| `jthread` inside `FuzzyIndex` | Task 2 |
| `clear` immediate publish | Task 1 |
| No HTTP flush endpoint | Global / out of scope |
| Unit delayed visibility + flush + bulk defer | Tasks 1–2 |
| TSan concurrent E3 | Task 4 |
| CLI error without publish-swap | Task 3 |
| Probe writes/s + p99; REFINEMENT changelog | Task 5 |
| Do not flip defaults | Global + Task 5 decision |
| Micro gate legacy | Task 5 |

**Placeholder scan:** none (`TBD` / “implement later” absent).

**Type consistency:** `std::chrono::milliseconds consolidate_ms`, `consolidate_ms_from_env()`, `dirty_`, `publish_draft_unlocked()`, `deferred_publish_active()`, `start_consolidator_if_needed()` / `stop_consolidator()` used consistently across tasks.

**Note:** Task 1 ships without the worker so TDD can lock “not visible until prepare” without races; Task 2 adds the Sonic timer. Do not skip Task 1 tests when implementing Task 2.
