# E2 Publish-Swap Concurrency Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Opt-in publish-swap mode on `FuzzyIndex` (copy → mutate → `atomic` publish) so readers never take `unique_lock`, improving mixed `/search` p99 vs E1 while keeping default `shared_mutex` unchanged.

**Architecture:** Extract `IndexState` (docs + Trie + FuzzyBackend). Add `FuzzyBackend::clone()` and Trie deep copy. `PublishMode::Legacy` keeps today’s shared_mutex in-place mutation; `PublishMode::PublishSwap` serializes writers and publishes via `std::atomic<std::shared_ptr<const IndexState>>`. Wire `--publish-swap` / `HOUND_PUBLISH_SWAP=1`. Re-run an adjusted E1 probe and record results in `docs/REFINEMENT.md`.

**Tech Stack:** C++20 (`std::atomic<std::shared_ptr<T>>`), Catch2, existing Release `hound` + hey probe under `scripts/tmp/`.

**Spec:** `docs/superpowers/specs/2026-07-24-e2-publish-swap-design.md`

## Global Constraints

- Default concurrency remains `shared_mutex` (`PublishMode::Legacy`); no public JSON break.
- Flag: `--publish-swap` / `HOUND_PUBLISH_SWAP=1` (default off).
- Deep-copy per mutation on flag-on; Insert/write regression under flag-on is **justified** in changelog.
- Micro `compare_bench.py` gate applies to **flag-off** only vs `baselines/micro_baseline.json`.
- `./scripts/run_correctness.sh` must pass (includes TSan when libtsan present).
- Probe scripts stay under `scripts/tmp/` (gitignored); do not commit them.
- Core headers stay HTTP/CSV-free.
- Conventional commits; English docs/comments.
- Do not start E3 (batched/background consolidate) in this plan.

## File structure

| Path | Role |
|------|------|
| `include/hound/trie.hpp` | Deep copy ctor / assign |
| `include/hound/fuzzy_backend.hpp` | `clone()` pure virtual |
| `include/hound/symspell_backend.hpp` | `clone()` impl |
| `include/hound/bk_fuzzy_backend.hpp` | `clone()` + BkTree deep copy |
| `include/hound/bk_tree.hpp` | Deep copy for BK clone |
| `include/hound/fuzzy_index.hpp` | `IndexState`, `PublishMode`, dual paths |
| `src/main.cpp` | CLI/env for publish-swap |
| `README.md` | One-liner for flag |
| `tests/unit/test_trie.cpp` | Copy tests |
| `tests/unit/test_fuzzy_backend.cpp` / new clone cases | Backend clone |
| `tests/unit/test_fuzzy_index.cpp` | Publish-swap visibility |
| `tests/concurrency/test_tsan_rw.cpp` | Second case flag-on |
| `scripts/tmp/probe_e2_mixed_load.sh` | Adjusted probe (untracked) |
| `scripts/tmp/e1_writer.py` | Preserve query doc (untracked tweak) |
| `docs/REFINEMENT.md` | Status + changelog |

---

### Task 1: Trie deep copy

**Files:**
- Modify: `include/hound/trie.hpp`
- Test: `tests/unit/test_trie.cpp`

**Interfaces:**
- Consumes: existing `Trie` API
- Produces: `Trie(const Trie&)`, `Trie& operator=(const Trie&)`, move left as defaulted/usable; copy is deep (independent trees)

- [ ] **Step 1: Write the failing test**

Append to `tests/unit/test_trie.cpp`:

```cpp
TEST_CASE("trie deep copy is independent", "[trie]") {
  hound::Trie a;
  a.insert("alpha", "1");
  a.insert("alpine", "2");
  hound::Trie b = a;
  b.erase("alpha", "1");
  REQUIRE(a.contains("alpha"));
  REQUIRE_FALSE(b.contains("alpha"));
  REQUIRE(b.contains("alpine"));
  auto comps = a.completions("alp", 10);
  REQUIRE(comps.size() == 2);
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build -j"$(nproc)" --target hound_tests
./build/hound_tests "[trie]"
```

Expected: FAIL to compile (`Trie` not copyable because of `unique_ptr`) or link/runtime failure.

- [ ] **Step 3: Implement deep copy on `Trie`**

In `include/hound/trie.hpp`, add public copy/move and private helpers (after `clear()`):

```cpp
  Trie(const Trie& other) : root_(clone_node(other.root_.get())) {}

  Trie& operator=(const Trie& other) {
    if (this != &other) {
      root_ = clone_node(other.root_.get());
    }
    return *this;
  }

  Trie(Trie&&) noexcept = default;
  Trie& operator=(Trie&&) noexcept = default;
```

In `private:`:

```cpp
  static std::unique_ptr<Node> clone_node(const Node* src) {
    if (!src) {
      return nullptr;
    }
    auto out = std::make_unique<Node>();
    out->ids = src->ids;
    out->is_terminal = src->is_terminal;
    for (const auto& [ch, child] : src->children) {
      out->children.emplace(ch, clone_node(child.get()));
    }
    return out;
  }
```

Keep default ctor as today (`root_ = std::make_unique<Node>()`).

- [ ] **Step 4: Run tests**

```bash
cmake --build build -j"$(nproc)" --target hound_tests
./build/hound_tests "[trie]"
```

Expected: all `[trie]` PASS.

- [ ] **Step 5: Commit**

```bash
git add include/hound/trie.hpp tests/unit/test_trie.cpp
git commit -m "$(cat <<'EOF'
feat(trie): support deep copy for publish-swap state

EOF
)"
```

---

### Task 2: `FuzzyBackend::clone()` + BK/SymSpell

**Files:**
- Modify: `include/hound/fuzzy_backend.hpp`
- Modify: `include/hound/bk_tree.hpp` (deep copy)
- Modify: `include/hound/bk_fuzzy_backend.hpp`
- Modify: `include/hound/symspell_backend.hpp`
- Test: `tests/unit/test_fuzzy_backend.cpp` (append) and/or `tests/unit/test_symspell_backend.cpp`

**Interfaces:**
- Consumes: Task 1 unrelated
- Produces: `virtual std::unique_ptr<FuzzyBackend> clone() const = 0;` on `FuzzyBackend`; both backends implement; clone is independent (mutate clone must not affect original search hits)

- [ ] **Step 1: Write failing clone tests**

Append to `tests/unit/test_symspell_backend.cpp` (or `test_fuzzy_backend.cpp`):

```cpp
TEST_CASE("symspell clone is independent", "[symspell][clone]") {
  auto a = std::make_unique<hound::SymSpellFuzzyBackend>();
  a->insert("alpha", "1");
  a->prepare();
  auto b = a->clone();
  REQUIRE(b != nullptr);
  b->erase("alpha", "1");
  auto ha = a->search("alpha", 0);
  REQUIRE_FALSE(ha.empty());
  auto hb = b->search("alpha", 0);
  REQUIRE(hb.empty());
}

TEST_CASE("bk clone is independent", "[bk][clone]") {
  auto a = std::make_unique<hound::BkFuzzyBackend>();
  a->insert("alpha", "1");
  auto b = a->clone();
  b->erase("alpha", "1");
  REQUIRE_FALSE(a->search("alpha", 0).empty());
  REQUIRE(b->search("alpha", 0).empty());
}
```

- [ ] **Step 2: Run to verify fail**

```bash
cmake --build build -j"$(nproc)" --target hound_tests
./build/hound_tests "[clone]"
```

Expected: FAIL compile (`clone` not declared).

- [ ] **Step 3: Add `clone()` to interface**

In `include/hound/fuzzy_backend.hpp`, after `prepare()`:

```cpp
  // Deep copy for publish-swap (E2). Independent of `this`.
  virtual std::unique_ptr<FuzzyBackend> clone() const = 0;
```

- [ ] **Step 4: BkTree deep copy + BkFuzzyBackend::clone**

In `include/hound/bk_tree.hpp`, add:

```cpp
  BkTree() = default;
  BkTree(const BkTree& other) : root_(clone_node(other.root_.get())) {}
  BkTree& operator=(const BkTree& other) {
    if (this != &other) {
      root_ = clone_node(other.root_.get());
    }
    return *this;
  }
  BkTree(BkTree&&) noexcept = default;
  BkTree& operator=(BkTree&&) noexcept = default;
```

Private:

```cpp
  static std::unique_ptr<Node> clone_node(const Node* src) {
    if (!src) {
      return nullptr;
    }
    auto out = std::make_unique<Node>(src->key);
    out->ids = src->ids;
    out->deleted = src->deleted;
    for (const auto& [edge, child] : src->children) {
      out->children.emplace(edge, clone_node(child.get()));
    }
    return out;
  }
```

In `BkFuzzyBackend`:

```cpp
  std::unique_ptr<FuzzyBackend> clone() const override {
    auto out = std::make_unique<BkFuzzyBackend>();
    out->tree_ = tree_;
    return out;
  }
```

- [ ] **Step 5: SymSpellFuzzyBackend::clone**

Copy `max_dict_edits_` + `dictionary_`. If `deletes_ready_`, also copy delete maps under `rebuild_mu_`; else leave maps empty and `deletes_ready_ = false`. Each clone gets its **own** `rebuild_mu_` (default-constructed — do not copy the mutex).

```cpp
  std::unique_ptr<FuzzyBackend> clone() const override {
    auto out = std::make_unique<SymSpellFuzzyBackend>(max_dict_edits_);
    out->dictionary_ = dictionary_;
    std::lock_guard lock(rebuild_mu_);
    if (deletes_ready_) {
      out->deletes_ = deletes_;
      out->multi_postings_ = multi_postings_;
      out->words_by_id_ = words_by_id_;
      out->word_to_id_ = word_to_id_;
      out->deletes_ready_ = true;
    } else {
      out->deletes_ready_ = false;
    }
    return out;
  }
```

(Make `clone` a friend or keep members accessible — class method can set private fields of `out`.)

- [ ] **Step 6: Run tests**

```bash
cmake --build build -j"$(nproc)" --target hound_tests
./build/hound_tests "[clone]"
```

Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add include/hound/fuzzy_backend.hpp include/hound/bk_tree.hpp \
  include/hound/bk_fuzzy_backend.hpp include/hound/symspell_backend.hpp \
  tests/unit/test_symspell_backend.cpp tests/unit/test_fuzzy_backend.cpp
git commit -m "$(cat <<'EOF'
feat(fuzzy): add FuzzyBackend::clone for publish-swap

EOF
)"
```

---

### Task 3: `IndexState` + `PublishMode` on `FuzzyIndex`

**Files:**
- Modify: `include/hound/fuzzy_index.hpp`
- Test: `tests/unit/test_fuzzy_index.cpp`

**Interfaces:**
- Consumes: `Trie` copy (Task 1), `FuzzyBackend::clone()` (Task 2)
- Produces:
  - `enum class PublishMode { Legacy, PublishSwap };`
  - `FuzzyIndex(std::unique_ptr<FuzzyBackend>, std::unique_ptr<Ranker>, PublishMode mode = PublishMode::Legacy)`
  - Legacy: in-place mutate under `shared_mutex` (behavior unchanged)
  - PublishSwap: `writer_mu_` + `std::atomic<std::shared_ptr<const IndexState>>`; reads load snapshot; writes copy→mutate→`prepare()` on fuzzy→store

- [ ] **Step 1: Write failing publish-swap tests**

Append to `tests/unit/test_fuzzy_index.cpp`:

```cpp
TEST_CASE("publish-swap upsert visible to search", "[fuzzy_index][publish_swap]") {
  hound::FuzzyIndex idx(hound::make_default_fuzzy_backend(), hound::make_default_ranker(),
                        hound::PublishMode::PublishSwap);
  idx.upsert({"1", "Alpha Ridge", 10.0});
  idx.prepare();
  auto hits = idx.search("alpha ridge", {.limit = 5});
  REQUIRE_FALSE(hits.empty());
  REQUIRE(hits.front().id == "1");
  REQUIRE(idx.erase("1"));
  auto after = idx.search("alpha ridge", {.limit = 5});
  for (const auto& h : after) {
    REQUIRE(h.id != "1");
  }
}

TEST_CASE("legacy mode still default", "[fuzzy_index][publish_swap]") {
  hound::FuzzyIndex idx;
  idx.upsert({"1", "Alpha Ridge", 10.0});
  auto hits = idx.search("alpha ridge", {.limit = 5});
  REQUIRE_FALSE(hits.empty());
}
```

- [ ] **Step 2: Run to verify fail**

```bash
cmake --build build -j"$(nproc)" --target hound_tests
./build/hound_tests "[publish_swap]"
```

Expected: FAIL compile (`PublishMode` unknown).

- [ ] **Step 3: Implement `IndexState` + dual mode in `fuzzy_index.hpp`**

Add includes: `<atomic>`, keep `<memory>`, `<shared_mutex>`, `<mutex>`.

Before `class FuzzyIndex`:

```cpp
enum class PublishMode { Legacy, PublishSwap };

struct IndexState {
  std::unordered_map<std::string, Document> docs;
  Trie trie;
  std::unique_ptr<FuzzyBackend> fuzzy;

  IndexState() : fuzzy(make_default_fuzzy_backend()) {}
  explicit IndexState(std::unique_ptr<FuzzyBackend> f) : fuzzy(std::move(f)) {
    if (!fuzzy) {
      fuzzy = make_default_fuzzy_backend();
    }
  }

  IndexState(const IndexState& other)
      : docs(other.docs), trie(other.trie), fuzzy(other.fuzzy ? other.fuzzy->clone() : nullptr) {
    if (!fuzzy) {
      fuzzy = make_default_fuzzy_backend();
    }
  }
};
```

Change ctor to accept `PublishMode mode = PublishMode::Legacy`. Initialize:

- Always create initial state from moved `fuzzy`.
- If `Legacy`: store in `legacy_` members (or a non-atomic `IndexState state_` + `shared_mutex`) — **prefer one `IndexState state_` + `shared_mutex mu_` for Legacy** to avoid duplicating field layouts.
- If `PublishSwap`: `published_.store(std::make_shared<const IndexState>(...))`, empty unused legacy fields OR only use publish path.

Recommended single layout:

```cpp
class FuzzyIndex {
 public:
  explicit FuzzyIndex(std::unique_ptr<FuzzyBackend> fuzzy = make_default_fuzzy_backend(),
                      std::unique_ptr<Ranker> ranker = make_default_ranker(),
                      PublishMode mode = PublishMode::Legacy);

  // same public API as today...

 private:
  PublishMode mode_;
  std::unique_ptr<Ranker> ranker_;

  // Legacy
  mutable std::shared_mutex mu_;
  IndexState state_;  // mutated in place

  // PublishSwap
  mutable std::mutex writer_mu_;
  std::atomic<std::shared_ptr<const IndexState>> published_;
};
```

Ctor body sketch:

```cpp
FuzzyIndex::... {
  if (!fuzzy) fuzzy = make_default_fuzzy_backend();
  if (!ranker) ranker = make_default_ranker();
  mode_ = mode;
  ranker_ = std::move(ranker);
  state_ = IndexState(std::move(fuzzy));
  if (mode_ == PublishMode::PublishSwap) {
    published_.store(std::make_shared<const IndexState>(state_), std::memory_order_release);
  }
}
```

`upsert` sketch (PublishSwap branch):

```cpp
void upsert(Document doc) {
  const std::string normalized = normalize(doc.text);
  if (mode_ == PublishMode::PublishSwap) {
    std::lock_guard wlock(writer_mu_);
    auto next = std::make_shared<IndexState>(*published_.load(std::memory_order_acquire));
    // same erase-old / insert-new logic on *next
    auto it = next->docs.find(doc.id);
    if (it != next->docs.end()) {
      const std::string old_norm = normalize(it->second.text);
      next->trie.erase(old_norm, doc.id);
      next->fuzzy->erase(old_norm, doc.id);
    }
    next->docs[doc.id] = doc;
    if (!normalized.empty()) {
      next->trie.insert(normalized, doc.id);
      next->fuzzy->insert(normalized, doc.id);
    }
    next->fuzzy->prepare();
    published_.store(std::shared_ptr<const IndexState>(std::move(next)),
                     std::memory_order_release);
    return;
  }
  // existing unique_lock path on state_
  ...
}
```

`search` PublishSwap: `auto snap = published_.load(...);` then run trie/fuzzy/docs logic against `*snap` (no `shared_lock`). Ranking unchanged via `ranker_`.

`erase` / `clear` / `prepare` / `get` / `size` / `copy_documents`: mirror the same branch pattern.

**Important:** Legacy path must keep using `state_` under `mu_` exactly as today’s semantics (tests without PublishMode must pass unchanged).

- [ ] **Step 4: Run unit tests**

```bash
cmake --build build -j"$(nproc)" --target hound_tests
./build/hound_tests "[fuzzy_index]"
```

Expected: PASS including `[publish_swap]`.

- [ ] **Step 5: Commit**

```bash
git add include/hound/fuzzy_index.hpp tests/unit/test_fuzzy_index.cpp
git commit -m "$(cat <<'EOF'
feat(core): add opt-in publish-swap concurrency mode

EOF
)"
```

---

### Task 4: CLI / env / README

**Files:**
- Modify: `src/main.cpp`
- Modify: `README.md` (short note near `--ranker` / concurrency)
- Optional tiny unit: parse helper if extracted; otherwise exercise via main flags only

**Interfaces:**
- Consumes: `PublishMode` from Task 3
- Produces: `publish_mode_from_env()`, `--publish-swap` flag (boolean switch, no value); env `HOUND_PUBLISH_SWAP=1|true|yes` enables; CLI overrides env when present

- [ ] **Step 1: Add env helper in `fuzzy_index.hpp` (or next to other factories)**

```cpp
inline PublishMode publish_mode_from_env() {
  const char* raw = std::getenv("HOUND_PUBLISH_SWAP");
  if (raw == nullptr || raw[0] == '\0') {
    return PublishMode::Legacy;
  }
  const std::string_view v(raw);
  if (v == "1" || v == "true" || v == "TRUE" || v == "yes" || v == "on") {
    return PublishMode::PublishSwap;
  }
  return PublishMode::Legacy;
}
```

(Needs `#include <cstdlib>` if not already via other headers — add in `fuzzy_index.hpp`.)

- [ ] **Step 2: Wire `main.cpp`**

- Extend `usage()` with `--publish-swap` and env note.
- Before constructing index: `auto publish_mode = hound::publish_mode_from_env();`
- Parse `--publish-swap` (no value): set `publish_mode = PublishMode::PublishSwap`.
- Construct: `FuzzyIndex index(..., ..., publish_mode);`
- Log line: `std::cerr << "publish mode: " << (publish_mode == PublishMode::PublishSwap ? "publish_swap" : "legacy") << "\n";`

- [ ] **Step 3: README**

Add one bullet: optional `--publish-swap` / `HOUND_PUBLISH_SWAP=1` for E2 publish-swap (default shared_mutex; write path slower).

- [ ] **Step 4: Smoke binary**

```bash
cmake --build build -j"$(nproc)" --target hound
./build/hound --help | grep -q publish-swap
```

Expected: grep succeeds; help mentions flag.

- [ ] **Step 5: Commit**

```bash
git add src/main.cpp include/hound/fuzzy_index.hpp README.md
git commit -m "$(cat <<'EOF'
feat(cli): wire --publish-swap and HOUND_PUBLISH_SWAP

EOF
)"
```

---

### Task 5: TSan coverage for publish-swap

**Files:**
- Modify: `tests/concurrency/test_tsan_rw.cpp`

**Interfaces:**
- Consumes: `PublishMode::PublishSwap` ctor
- Produces: second Catch test that runs concurrent search+upsert under publish-swap (same shape as existing test)

- [ ] **Step 1: Add test case**

```cpp
TEST_CASE("concurrent search with upsert under TSan (publish-swap)",
          "[tsan][concurrency][publish_swap]") {
  hound::FuzzyIndex index(hound::make_default_fuzzy_backend(), hound::make_default_ranker(),
                          hound::PublishMode::PublishSwap);
  // identical body to existing test (seed, searcher, writer, join, REQUIRE)
  ...
}
```

Duplicate the body from the existing test (do not factor unless trivial — YAGNI). Ensure `prepare()` is not required mid-loop (upsert path already calls `prepare()` in PublishSwap).

- [ ] **Step 2: Run correctness (includes TSan if available)**

```bash
./scripts/run_correctness.sh
```

Expected: `correctness OK` (or WARN skip TSan if no libtsan — still unit/integration green).

- [ ] **Step 3: Commit**

```bash
git add tests/concurrency/test_tsan_rw.cpp
git commit -m "$(cat <<'EOF'
test(tsan): exercise publish-swap under concurrent R/W

EOF
)"
```

---

### Task 6: Adjusted probe + measure + REFINEMENT

**Files:**
- Create (untracked): `scripts/tmp/probe_e2_mixed_load.sh`
- Modify (untracked): `scripts/tmp/e1_writer.py` — add `--skip-ids` or hardcode skip `doc-0`
- Modify (tracked): `docs/REFINEMENT.md`
- Optional: flag-off micro gate only if Task 3 touched hot path layout for legacy

**Interfaces:**
- Consumes: Release `hound` with/without `--publish-swap`
- Produces: `benchmarks/results/e2_mixed_legacy_*.txt`, `e2_mixed_swap_*.txt`; changelog with p99 deltas

- [ ] **Step 1: Writer preserves query doc**

In `scripts/tmp/e1_writer.py`, add `--exclude-mod N` (default `1`): rotate `doc-{(i % (docs-exclude))+exclude}` so `doc-0` never overwritten when `exclude=1`. Or `--skip-prefix doc-0` single id. Document in probe header.

Example change in worker loop:

```python
# skip first `exclude` ids so hey query on doc-0 stays stable
span = max(docs - exclude, 1)
doc_id = f"doc-{(i % span) + exclude}"
```

Pass `--exclude 1` from the probe.

- [ ] **Step 2: Write `scripts/tmp/probe_e2_mixed_load.sh`**

Based on E1 probe, but:

1. Scenarios: **search-only → mixed** only (write-only optional at end, not before mixed).
2. Start hound with extra args from `HOUND_E2_HOUND_ARGS` (e.g. `--publish-swap`).
3. Writer: `--exclude 1`.
4. Output: `benchmarks/results/e2_mixed_${LABEL}_${STAMP}.txt` where `LABEL=legacy|swap`.

Do **not** `git add` these scripts.

- [ ] **Step 3: Run legacy probe**

```bash
HOUND_E2_HOUND_ARGS= ./scripts/tmp/probe_e2_mixed_load.sh
# or explicitly without publish-swap
```

Record mixed exact/typo p99.

- [ ] **Step 4: Run publish-swap probe**

```bash
HOUND_E2_HOUND_ARGS=--publish-swap ./scripts/tmp/probe_e2_mixed_load.sh
```

Record mixed p99 + writes/s.

- [ ] **Step 5: Flag-off micro gate (if legacy path refactored)**

```bash
./scripts/run_micro.sh benchmarks/results/micro_e2_legacy.json
./scripts/compare_bench.py baselines/micro_baseline.json benchmarks/results/micro_e2_legacy.json
```

Expected: pass, or justify.

- [ ] **Step 6: Update `docs/REFINEMENT.md`**

- Status: E2 done (or iterate); next E3 only if p99 did not improve / write path unusable.
- Phase E table: E2 status.
- Phase 2 changelog with real numbers (template from spec).
- Note: Insert/write cost under publish-swap justified; probe excluded `doc-0`.

- [ ] **Step 7: Commit docs only**

```bash
git add docs/REFINEMENT.md
git status  # confirm scripts/tmp not staged
git commit -m "$(cat <<'EOF'
docs: record Phase E2 publish-swap probe results

EOF
)"
```

---

## Self-review (plan vs spec)

| Spec requirement | Task |
|------------------|------|
| `IndexState` docs+trie+fuzzy | Task 3 |
| `FuzzyBackend::clone` SymSpell+BK | Task 2 |
| Trie deep copy | Task 1 |
| Dual mode Legacy / PublishSwap | Task 3 |
| `--publish-swap` / `HOUND_PUBLISH_SWAP` | Task 4 |
| Ranker outside state | Task 3 |
| Unit visibility tests | Task 3 |
| TSan concurrent | Task 5 |
| Probe order + preserve query text | Task 6 |
| REFINEMENT changelog + E3 decision gate | Task 6 |
| No default flip / no E3 impl | Global + Task 6 decision |
| Micro gate flag-off | Task 6 |
| HttpApi minimal | Deferred: API `mu_` may remain; readers unlocked in core — sufficient for DoD |

**HttpApi note:** Spec allows keeping a short API write lock. This plan does **not** require changing `http_api.hpp` unless probe shows API mutex dominating after publish-swap; if so, iterate with a follow-up commit (still E2).

No TBD placeholders in task bodies. Names: `PublishMode`, `IndexState`, `clone()`, `published_`, `writer_mu_` consistent across tasks.
