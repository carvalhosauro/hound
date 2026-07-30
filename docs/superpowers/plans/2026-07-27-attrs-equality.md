# Attrs equality (H1) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add optional string `attrs` on documents and AND equality filters on
`GET /search` without breaking the v0.1.0 wire shape when attrs/filters are omitted.

**Architecture:** Extend `Document` with `std::map<std::string,std::string> attrs`;
maintain inverted `attr_postings` on `IndexState` (copied on publish-swap);
intersect gather candidates with the eligible id set **before** rank when
`SearchOptions::attr_filters` is non-empty. HTTP parses `attrs` on write and
`attrs.*` query params on search. Snapshot bumps to v2.

**Tech Stack:** C++20, existing `FuzzyIndex` / httplib / nlohmann::json, Catch2,
CMake.

**Spec:** [`docs/superpowers/specs/2026-07-27-attrs-equality-design.md`](../specs/2026-07-27-attrs-equality-design.md)

## Methodology — test-first + measure trade-offs

**Yes: this plan is test-first (TDD).** Every behavior task follows:

```text
RED  → write failing Catch2 test(s) first (or extend existing)
GREEN → minimal implementation to pass
REFACTOR → clean up; keep tests green
MEASURE → when the hot path changes, record numbers before claiming wins
COMMIT → one conventional commit per green task
```

Do **not** implement filter/postings/HTTP before the corresponding tests exist and
fail for the right reason (compile error or assertion).

**Measure what (trade-offs to quantify, not guess):**

| Question | How |
|----------|-----|
| Unfiltered path regress vs v0.1.0? | Gate micro: `BM_SearchFuzzy/20000/{1,2}`, `BM_SearchExact/20000`, `BM_Insert/20000` vs `baselines/micro_baseline.json` (+10% `cpu_time`) |
| Cost of attrs on ingest? | New micro: insert N docs **with** 1–2 attrs vs without (same N) |
| Cost of filtered search? | New micro: fuzzy @20k with `attrs.tenant=…` (1 of K tenants) vs same query unfiltered |
| Under-fetch risk (global candidates ∩ filter)? | Fixture: many tenants share identical `text`; low `limit` + filter → assert short/empty vs higher `limit`; document in REFINEMENT changelog with counts |
| Correctness without attrs? | Full `./scripts/run_correctness.sh` after each core/HTTP task |

Record before/after (or with/without filter) in the Phase 2 changelog entry for H1.
Never flip defaults or accept micro baseline without human decision.

## Global Constraints

- No domain types in core (no `establishment_id` symbol).
- Attr values are strings only on the JSON wire.
- Search hit JSON unchanged (no attrs in response).
- Omit filters → identical behavior to v0.1.0.
- Filter **before** rank (eligible ∩ candidates → rank → limit).
- Omit `attrs` on upsert → empty map (wipe).
- English user-facing docs; synthetic test data only.
- `./scripts/run_correctness.sh` must stay green; micro gate if search hot path
  changes (`BM_SearchFuzzy/20000/1`, `/2`, `BM_SearchExact/20000`, `BM_Insert/20000`).
- Update OpenAPI + compat in the same change set as HTTP.
- Core headers stay HTTP/CSV-free (`document.hpp` / `fuzzy_index.hpp` / `snapshot.hpp`).

## File structure

| Path | Role |
|------|------|
| `include/hound/document.hpp` | `Document::attrs` |
| `include/hound/fuzzy_index.hpp` | `SearchOptions::attr_filters`, `IndexState::attr_postings`, upsert/erase/clear/search |
| `include/hound/http_api.hpp` | Parse `attrs` body + `attrs.*` query |
| `include/hound/snapshot.hpp` | v2 read/write attrs |
| `include/hound/bulk_loader.hpp` | JSON `--load` optional `attrs` object |
| `CMakeLists.txt` | Add `tests/unit/test_attrs_filter.cpp` to `hound_tests` |
| `tests/unit/test_attrs_filter.cpp` | Core filter / postings tests |
| `tests/unit/test_snapshot.cpp` | v2 roundtrip + v1 reject |
| `tests/unit/test_bulk_loader.cpp` | JSON attrs load |
| `tests/integration/test_http_api.cpp` | HTTP attrs + filter |
| `docs/openapi.yaml`, `compat.md`, `search-params.md`, `errors.md`, `snapshot.md`, `REFINEMENT.md`, `CHANGELOG.md`, `README.md` | Wire + roadmap |

---

### Task 1: Document model + failing unit tests

**Files:**
- Modify: `include/hound/document.hpp`
- Create: `tests/unit/test_attrs_filter.cpp`
- Modify: `CMakeLists.txt` (explicit source list — must add the new file)

**Interfaces:**
- Produces: `Document::attrs` as `std::map<std::string, std::string>`
- Produces: failing tests that call `SearchOptions::attr_filters` / filtered
  `search` (added in Task 2)

- [ ] **Step 1: Extend `Document`**

In `include/hound/document.hpp`:

```cpp
#pragma once

#include <map>
#include <string>

namespace hound {

struct Document {
  std::string id;
  std::string text;
  double external_score = 0.0;
  std::map<std::string, std::string> attrs;
};

struct SearchHit {
  std::string id;
  double score = 0.0;
  double text_relevance = 0.0;
  double external_score = 0.0;
};

}  // namespace hound
```

- [ ] **Step 2: Add test source to CMake**

In `CMakeLists.txt`, inside `add_executable(hound_tests …)`, after
`tests/unit/test_fuzzy_index.cpp` add:

```cmake
      tests/unit/test_attrs_filter.cpp
```

- [ ] **Step 3: Write failing unit tests**

Create `tests/unit/test_attrs_filter.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <thread>

#include "hound/fuzzy_index.hpp"

TEST_CASE("attrs filter AND equality", "[attrs][h1]") {
  hound::FuzzyIndex index;
  index.upsert({"1", "Queijo Mussarela", 1.0, {{"tenant", "a"}}});
  index.upsert({"2", "Queijo Mussarela", 9.0, {{"tenant", "b"}}});
  hound::SearchOptions opt;
  opt.limit = 10;
  opt.attr_filters = {{"tenant", "a"}};
  auto hits = index.search("queijo", opt);
  REQUIRE(hits.size() == 1);
  REQUIRE(hits[0].id == "1");
}

TEST_CASE("attrs omit filter keeps both", "[attrs][h1]") {
  hound::FuzzyIndex index;
  index.upsert({"1", "Queijo Mussarela", 1.0, {{"tenant", "a"}}});
  index.upsert({"2", "Queijo Mussarela", 9.0, {{"tenant", "b"}}});
  auto hits = index.search("queijo", {.limit = 10});
  REQUIRE(hits.size() == 2);
  REQUIRE(hits[0].id == "2");  // higher external_score
  REQUIRE(hits[1].id == "1");
}

TEST_CASE("attrs missing key excludes doc", "[attrs][h1]") {
  hound::FuzzyIndex index;
  index.upsert({"1", "Queijo", 1.0, {{"tenant", "a"}}});
  index.upsert({"3", "Queijo", 9.0, {}});  // no attrs
  hound::SearchOptions opt;
  opt.limit = 10;
  opt.attr_filters = {{"tenant", "a"}};
  auto hits = index.search("queijo", opt);
  REQUIRE(hits.size() == 1);
  REQUIRE(hits[0].id == "1");
}

TEST_CASE("attrs AND requires all keys", "[attrs][h1]") {
  hound::FuzzyIndex index;
  index.upsert({"1", "Item", 1.0, {{"tenant", "a"}, {"open", "1"}}});
  index.upsert({"2", "Item", 9.0, {{"tenant", "a"}, {"open", "0"}}});
  hound::SearchOptions opt;
  opt.limit = 10;
  opt.attr_filters = {{"tenant", "a"}, {"open", "1"}};
  auto hits = index.search("item", opt);
  REQUIRE(hits.size() == 1);
  REQUIRE(hits[0].id == "1");
}

TEST_CASE("attrs upsert replaces map wholesale", "[attrs][h1]") {
  hound::FuzzyIndex index;
  index.upsert({"1", "Item", 1.0, {{"tenant", "a"}}});
  index.upsert({"1", "Item", 1.0, {{"tenant", "b"}}});
  hound::SearchOptions opt_a{.limit = 10, .attr_filters = {{"tenant", "a"}}};
  hound::SearchOptions opt_b{.limit = 10, .attr_filters = {{"tenant", "b"}}};
  REQUIRE(index.search("item", opt_a).empty());
  auto hits = index.search("item", opt_b);
  REQUIRE(hits.size() == 1);
  REQUIRE(hits[0].id == "1");
}

TEST_CASE("attrs erase removes from postings", "[attrs][h1]") {
  hound::FuzzyIndex index;
  index.upsert({"1", "Item", 1.0, {{"tenant", "a"}}});
  REQUIRE(index.erase("1"));
  hound::SearchOptions opt{.limit = 10, .attr_filters = {{"tenant", "a"}}};
  REQUIRE(index.search("item", opt).empty());
}

TEST_CASE("attrs unknown filter value yields no hits", "[attrs][h1]") {
  hound::FuzzyIndex index;
  index.upsert({"1", "Item", 1.0, {{"tenant", "a"}}});
  hound::SearchOptions opt{.limit = 10, .attr_filters = {{"tenant", "z"}}};
  REQUIRE(index.search("item", opt).empty());
}

TEST_CASE("attrs under-fetch: low limit can miss eligible after intersect", "[attrs][h1][tradeoff]") {
  // Many docs share the same text; only the last tenant is eligible.
  // With a tiny limit, global gather may not include the eligible id → empty.
  // Documents the intersect trade-off (callers: raise limit or wait for eligible-first).
  hound::FuzzyIndex index;
  for (int i = 0; i < 32; ++i) {
    index.upsert({std::to_string(i), "Shared Label", static_cast<double>(i),
                  {{"tenant", std::string(1, static_cast<char>('a' + (i % 26)))}}});
  }
  index.upsert({"eligible", "Shared Label", 0.0, {{"tenant", "zz"}}});
  hound::SearchOptions tight{.limit = 1, .attr_filters = {{"tenant", "zz"}}};
  hound::SearchOptions wide{.limit = 64, .attr_filters = {{"tenant", "zz"}}};
  // Wide must find eligible; tight may be empty — record both outcomes in assert comments
  // after GREEN: REQUIRE wide hits; INFO/CAPTURE tight size for changelog numbers.
  auto wide_hits = index.search("shared", wide);
  REQUIRE(wide_hits.size() == 1);
  REQUIRE(wide_hits[0].id == "eligible");
  auto tight_hits = index.search("shared", tight);
  // Soft assert: document actual size in CAPTURE (0 is the known risk).
  CAPTURE(tight_hits.size());
  SUCCEED("trade-off probe: tight_hits=" << tight_hits.size());
}

// --- High priority: wipe, empty value, filter-before-rank ---

TEST_CASE("attrs empty map on upsert wipes prior attrs", "[attrs][h1]") {
  hound::FuzzyIndex index;
  index.upsert({"1", "Item", 1.0, {{"tenant", "a"}}});
  index.upsert({"1", "Item", 1.0, {}});  // empty map = wipe
  hound::SearchOptions opt{.limit = 10, .attr_filters = {{"tenant", "a"}}};
  REQUIRE(index.search("item", opt).empty());
  auto doc = index.get("1");
  REQUIRE(doc.has_value());
  REQUIRE(doc->attrs.empty());
}

TEST_CASE("attrs empty string value is filterable", "[attrs][h1]") {
  hound::FuzzyIndex index;
  index.upsert({"1", "Item", 1.0, {{"open", ""}}});
  index.upsert({"2", "Item", 9.0, {{"open", "1"}}});
  hound::SearchOptions empty_v{.limit = 10, .attr_filters = {{"open", ""}}};
  hound::SearchOptions one{.limit = 10, .attr_filters = {{"open", "1"}}};
  auto hits_empty = index.search("item", empty_v);
  REQUIRE(hits_empty.size() == 1);
  REQUIRE(hits_empty[0].id == "1");
  auto hits_one = index.search("item", one);
  REQUIRE(hits_one.size() == 1);
  REQUIRE(hits_one[0].id == "2");
}

TEST_CASE("attrs filter ranks only within eligible set", "[attrs][h1]") {
  // Other tenant has higher external_score; must not win under filter.
  hound::FuzzyIndex index;
  index.upsert({"low", "Shared", 1.0, {{"tenant", "a"}}});
  index.upsert({"high", "Shared", 5.0, {{"tenant", "a"}}});
  index.upsert({"other", "Shared", 99.0, {{"tenant", "b"}}});
  hound::SearchOptions opt{.limit = 10, .attr_filters = {{"tenant", "a"}}};
  auto hits = index.search("shared", opt);
  REQUIRE(hits.size() == 2);
  REQUIRE(hits[0].id == "high");
  REQUIRE(hits[1].id == "low");
}

// --- Medium priority: clear, publish-swap copy, E3 deferred ---

TEST_CASE("attrs clear removes postings (legacy)", "[attrs][h1]") {
  hound::FuzzyIndex index;
  index.upsert({"1", "Item", 1.0, {{"tenant", "a"}}});
  index.clear();
  hound::SearchOptions opt{.limit = 10, .attr_filters = {{"tenant", "a"}}};
  REQUIRE(index.search("item", opt).empty());
  REQUIRE(index.size() == 0);
}

TEST_CASE("attrs clear removes postings (publish-swap)", "[attrs][h1][publish_swap]") {
  hound::FuzzyIndex index(hound::make_default_fuzzy_backend(), hound::make_default_ranker(),
                          hound::PublishMode::PublishSwap);
  index.upsert({"1", "Item", 1.0, {{"tenant", "a"}}});
  index.prepare();
  index.clear();
  hound::SearchOptions opt{.limit = 10, .attr_filters = {{"tenant", "a"}}};
  REQUIRE(index.search("item", opt).empty());
  REQUIRE(index.size() == 0);
}

TEST_CASE("attrs publish-swap copy keeps postings after prepare", "[attrs][h1][publish_swap]") {
  // PublishSwap copies IndexState; attr_postings must be included or filters break.
  hound::FuzzyIndex index(hound::make_default_fuzzy_backend(), hound::make_default_ranker(),
                          hound::PublishMode::PublishSwap);
  index.begin_bulk();
  index.upsert({"1", "Alpha Ridge", 10.0, {{"tenant", "a"}}});
  index.upsert({"2", "Alpha Ridge", 20.0, {{"tenant", "b"}}});
  REQUIRE(index.search("alpha", {.limit = 10, .attr_filters = {{"tenant", "a"}}}).empty());
  index.prepare();
  auto hits = index.search("alpha", {.limit = 10, .attr_filters = {{"tenant", "a"}}});
  REQUIRE(hits.size() == 1);
  REQUIRE(hits[0].id == "1");
}

TEST_CASE("attrs e3 deferred upsert not visible to filter until prepare", "[attrs][h1][e3]") {
  using namespace std::chrono_literals;
  hound::FuzzyIndex index(hound::make_default_fuzzy_backend(), hound::make_default_ranker(),
                          hound::PublishMode::PublishSwap, 500ms);
  index.upsert({"1", "Alpha Ridge", 10.0, {{"tenant", "a"}}});
  hound::SearchOptions opt{.limit = 10, .attr_filters = {{"tenant", "a"}}};
  REQUIRE(index.search("alpha", opt).empty());
  index.prepare();
  auto hits = index.search("alpha", opt);
  REQUIRE(hits.size() == 1);
  REQUIRE(hits[0].id == "1");
}
```

- [ ] **Step 4: Build and run — expect compile errors**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j"$(nproc)" --target hound_tests
```

Expected: FAIL to compile (`attr_filters` not a member of `SearchOptions`).

- [ ] **Step 5: Commit**

```bash
git add include/hound/document.hpp tests/unit/test_attrs_filter.cpp CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(index): add Document attrs field and filter tests (H1)

EOF
)"
```

---

### Task 2: Attr postings + search intersect in FuzzyIndex

**Files:**
- Modify: `include/hound/fuzzy_index.hpp`
- Modify: `tests/unit/test_attrs_filter.cpp` (should go green; no API renames)

**Interfaces:**
- Consumes: `Document::attrs`
- Produces: `SearchOptions::attr_filters` (`std::map<std::string,std::string>`);
  `IndexState::attr_postings`; search intersects before rank

- [ ] **Step 1: Extend `SearchOptions`**

After `std::optional<RankerKind> ranker;` in `SearchOptions`:

```cpp
  // Empty = no filter (v0.1.0 path). Non-empty = AND equality on attrs.
  std::map<std::string, std::string> attr_filters;
```

Add `#include <map>` (explicit).

- [ ] **Step 2: Extend `IndexState`**

Add member:

```cpp
  // key → value → ids
  std::unordered_map<std::string,
                     std::unordered_map<std::string, std::unordered_set<std::string>>>
      attr_postings;
```

Update copy ctor and `operator=` to copy `attr_postings` alongside `docs` /
`trie` / `fuzzy`:

```cpp
  IndexState(const IndexState& other)
      : docs(other.docs),
        trie(other.trie),
        fuzzy(other.fuzzy ? other.fuzzy->clone() : nullptr),
        attr_postings(other.attr_postings) {
    if (!fuzzy) {
      fuzzy = make_default_fuzzy_backend();
    }
  }

  IndexState& operator=(const IndexState& other) {
    if (this != &other) {
      docs = other.docs;
      trie = other.trie;
      fuzzy = other.fuzzy ? other.fuzzy->clone() : make_default_fuzzy_backend();
      attr_postings = other.attr_postings;
    }
    return *this;
  }
```

- [ ] **Step 3: Postings helpers + maintain on upsert/erase/clear**

Add private static helpers near `apply_upsert` (same file):

```cpp
  static void remove_attr_postings(IndexState& st, const Document& doc) {
    for (const auto& [k, v] : doc.attrs) {
      auto kit = st.attr_postings.find(k);
      if (kit == st.attr_postings.end()) {
        continue;
      }
      auto vit = kit->second.find(v);
      if (vit == kit->second.end()) {
        continue;
      }
      vit->second.erase(doc.id);
      if (vit->second.empty()) {
        kit->second.erase(vit);
      }
      if (kit->second.empty()) {
        st.attr_postings.erase(kit);
      }
    }
  }

  static void insert_attr_postings(IndexState& st, const Document& doc) {
    for (const auto& [k, v] : doc.attrs) {
      st.attr_postings[k][v].insert(doc.id);
    }
  }
```

In `apply_upsert`, before replacing the doc, remove old postings; after store,
insert new:

```cpp
  static void apply_upsert(IndexState& st, Document doc, const std::string& normalized) {
    auto it = st.docs.find(doc.id);
    if (it != st.docs.end()) {
      const std::string old_norm = normalize(it->second.text);
      st.trie.erase(old_norm, doc.id);
      st.fuzzy->erase(old_norm, doc.id);
      remove_attr_postings(st, it->second);
    }
    st.docs[doc.id] = doc;
    insert_attr_postings(st, st.docs[doc.id]);
    if (!normalized.empty()) {
      st.trie.insert(normalized, doc.id);
      st.fuzzy->insert(normalized, doc.id);
    }
  }
```

In `apply_erase`, before erasing the doc:

```cpp
    remove_attr_postings(st, it->second);
    st.docs.erase(it);
```

In `clear()`, clear postings with the docs (both branches):

```cpp
      draft_.attr_postings.clear();  // PublishSwap branch (with docs/trie/fuzzy)
      // ...
    state_.attr_postings.clear();  // Legacy branch
```

- [ ] **Step 4: Intersect in `search` before rank**

Refactor `search` so eligible is computed from the same `IndexState` as gather.
Minimal shape: compute `eligible` at the start of `run_search`, skip
non-eligible ids in `consider`, then build `candidates` → rank → limit as today.

```cpp
    auto run_search = [&](const IndexState& st) {
      std::optional<std::unordered_set<std::string>> eligible;
      if (!opt.attr_filters.empty()) {
        std::unordered_set<std::string> set;
        bool first = true;
        for (const auto& [key, value] : opt.attr_filters) {
          auto kit = st.attr_postings.find(key);
          if (kit == st.attr_postings.end()) {
            set.clear();
            break;
          }
          auto vit = kit->second.find(value);
          if (vit == kit->second.end()) {
            set.clear();
            break;
          }
          if (first) {
            set = vit->second;
            first = false;
          } else {
            std::unordered_set<std::string> next;
            for (const auto& id : set) {
              if (vit->second.count(id)) {
                next.insert(id);
              }
            }
            set = std::move(next);
          }
          if (set.empty()) {
            break;
          }
        }
        eligible = std::move(set);
      }

      auto consider = [&](const std::string& id, int distance, bool prefix_bonus) {
        if (eligible && !eligible->count(id)) {
          return;
        }
        // ... existing consider body unchanged ...
      };

      // ... existing trie completions + fuzzy gather unchanged ...
    };
```

Then rank → limit unchanged. Empty `attr_filters` leaves `eligible` as
`nullopt` so the hot path does one extra null check only.

- [ ] **Step 5: Run unit tests green + correctness**

```bash
cmake --build build -j"$(nproc)" --target hound_tests
./build/hound_tests "[attrs]"
./scripts/run_correctness.sh
```

Expected: all `[attrs]` PASS; correctness green.

- [ ] **Step 6: Commit**

```bash
git add include/hound/fuzzy_index.hpp tests/unit/test_attrs_filter.cpp
git commit -m "$(cat <<'EOF'
feat(index): filter search candidates by attrs AND equality

EOF
)"
```

---

### Task 3: HTTP wire + integration tests

**Files:**
- Modify: `include/hound/http_api.hpp`
- Modify: `tests/integration/test_http_api.cpp`

**Interfaces:**
- Consumes: `Document::attrs`, `SearchOptions::attr_filters`
- Produces: JSON `attrs` object (string values only); query `attrs.<key>=<value>`

- [ ] **Step 1: Shared parse helper for attrs object**

Near the top of `http_api.hpp` (anonymous namespace or private static):

```cpp
inline std::map<std::string, std::string> parse_attrs_object(const nlohmann::json& body) {
  std::map<std::string, std::string> attrs;
  if (!body.contains("attrs")) {
    return attrs;
  }
  const auto& a = body.at("attrs");
  if (!a.is_object()) {
    throw std::runtime_error("attrs must be an object");
  }
  for (auto it = a.begin(); it != a.end(); ++it) {
    if (it.key().empty()) {
      throw std::runtime_error("attrs key must be non-empty");
    }
    if (!it.value().is_string()) {
      throw std::runtime_error("attrs values must be strings");
    }
    attrs.emplace(it.key(), it.value().get<std::string>());
  }
  return attrs;
}
```

In `POST /index` and each bulk item, after `external_score`:

```cpp
        doc.attrs = parse_attrs_object(body);  // or item
```

- [ ] **Step 2: Parse search `attrs.*` params**

`req.params` is `httplib::Params` = `std::multimap<std::string,std::string>`.
In `GET /search`, after existing optional params, before `index_.search`:

```cpp
        constexpr std::string_view kPrefix = "attrs.";
        for (const auto& [key, value] : req.params) {
          if (key.size() >= kPrefix.size() &&
              std::string_view(key).substr(0, kPrefix.size()) == kPrefix) {
            const std::string attr_key = key.substr(kPrefix.size());
            if (attr_key.empty()) {
              throw std::runtime_error("attrs key must be non-empty");
            }
            opt.attr_filters[attr_key] = value;  // last wins
          }
        }
```

- [ ] **Step 3: Integration tests**

Append to `tests/integration/test_http_api.cpp`:

```cpp
TEST_CASE("HTTP attrs filter equality", "[integration][http][attrs][h1]") {
  hound::FuzzyIndex index;
  hound::HttpApi api(index);
  const int port = api.server().bind_to_any_port("127.0.0.1");
  REQUIRE(port > 0);
  std::thread th([&] { api.server().listen_after_bind(); });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  httplib::Client client("127.0.0.1", port);
  client.set_connection_timeout(1, 0);
  client.set_read_timeout(1, 0);

  auto a = client.Post(
      "/index",
      R"({"id":"1","text":"Queijo Mussarela","external_score":1,"attrs":{"tenant":"a"}})",
      "application/json");
  REQUIRE(a);
  REQUIRE(a->status == 200);
  auto b = client.Post(
      "/index",
      R"({"id":"2","text":"Queijo Mussarela","external_score":9,"attrs":{"tenant":"b"}})",
      "application/json");
  REQUIRE(b);
  REQUIRE(b->status == 200);

  auto filtered = client.Get("/search?q=queijo&limit=10&attrs.tenant=a");
  REQUIRE(filtered);
  REQUIRE(filtered->status == 200);
  auto body = nlohmann::json::parse(filtered->body);
  REQUIRE(body["results"].size() == 1);
  REQUIRE(body["results"][0]["id"] == "1");
  // Hits must not include attrs (hydrate in app).
  REQUIRE_FALSE(body["results"][0].contains("attrs"));

  auto unfiltered = client.Get("/search?q=queijo&limit=10");
  REQUIRE(unfiltered);
  REQUIRE(unfiltered->status == 200);
  REQUIRE(nlohmann::json::parse(unfiltered->body)["results"].size() == 2);

  api.stop();
  th.join();
}

TEST_CASE("HTTP attrs upsert wipe and empty value", "[integration][http][attrs][h1]") {
  hound::FuzzyIndex index;
  hound::HttpApi api(index);
  const int port = api.server().bind_to_any_port("127.0.0.1");
  REQUIRE(port > 0);
  std::thread th([&] { api.server().listen_after_bind(); });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  httplib::Client client("127.0.0.1", port);
  client.set_connection_timeout(1, 0);
  client.set_read_timeout(1, 0);

  REQUIRE(client.Post(
              "/index",
              R"({"id":"1","text":"Item","external_score":1,"attrs":{"tenant":"a","open":""}})",
              "application/json")
              ->status == 200);

  auto empty_open = client.Get("/search?q=item&limit=10&attrs.open=");
  REQUIRE(empty_open);
  REQUIRE(empty_open->status == 200);
  REQUIRE(nlohmann::json::parse(empty_open->body)["results"].size() == 1);

  // Omit attrs → wipe prior map.
  REQUIRE(client.Post("/index", R"({"id":"1","text":"Item","external_score":1})",
                      "application/json")
              ->status == 200);
  auto after_wipe = client.Get("/search?q=item&limit=10&attrs.tenant=a");
  REQUIRE(after_wipe);
  REQUIRE(after_wipe->status == 200);
  REQUIRE(nlohmann::json::parse(after_wipe->body)["results"].empty());

  // Explicit empty object also wipes.
  REQUIRE(client.Post(
              "/index",
              R"({"id":"2","text":"Item","external_score":1,"attrs":{"tenant":"b"}})",
              "application/json")
              ->status == 200);
  REQUIRE(client.Post("/index",
                      R"({"id":"2","text":"Item","external_score":1,"attrs":{}})",
                      "application/json")
              ->status == 200);
  auto wiped_obj = client.Get("/search?q=item&limit=10&attrs.tenant=b");
  REQUIRE(wiped_obj);
  REQUIRE(nlohmann::json::parse(wiped_obj->body)["results"].empty());

  api.stop();
  th.join();
}

TEST_CASE("HTTP attrs validation 400s and last-wins", "[integration][http][attrs][h1]") {
  hound::FuzzyIndex index;
  hound::HttpApi api(index);
  const int port = api.server().bind_to_any_port("127.0.0.1");
  REQUIRE(port > 0);
  std::thread th([&] { api.server().listen_after_bind(); });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  httplib::Client client("127.0.0.1", port);
  client.set_connection_timeout(1, 0);
  client.set_read_timeout(1, 0);

  auto expect400 = [&](auto resp) {
    REQUIRE(resp);
    REQUIRE(resp->status == 400);
    auto j = nlohmann::json::parse(resp->body);
    REQUIRE(j.contains("error"));
    REQUIRE(j["error"].is_string());
  };

  expect400(client.Post(
      "/index", R"({"id":"3","text":"X","external_score":1,"attrs":{"tenant":1}})",
      "application/json"));
  expect400(client.Post(
      "/index", R"({"id":"3","text":"X","external_score":1,"attrs":[]})",
      "application/json"));
  expect400(client.Post(
      "/index", R"({"id":"3","text":"X","external_score":1,"attrs":"x"})",
      "application/json"));
  expect400(client.Post(
      "/index", R"({"id":"3","text":"X","external_score":1,"attrs":null})",
      "application/json"));
  expect400(client.Post(
      "/index", R"({"id":"3","text":"X","external_score":1,"attrs":{"":"v"}})",
      "application/json"));

  expect400(client.Get("/search?q=x&attrs.=v"));

  REQUIRE(client.Post(
              "/index",
              R"({"id":"1","text":"Queijo","external_score":1,"attrs":{"tenant":"a"}})",
              "application/json")
              ->status == 200);
  REQUIRE(client.Post(
              "/index",
              R"({"id":"2","text":"Queijo","external_score":9,"attrs":{"tenant":"b"}})",
              "application/json")
              ->status == 200);

  // Duplicate attrs.tenant → last wins (=b).
  auto last = client.Get("/search?q=queijo&limit=10&attrs.tenant=a&attrs.tenant=b");
  REQUIRE(last);
  REQUIRE(last->status == 200);
  auto results = nlohmann::json::parse(last->body)["results"];
  REQUIRE(results.size() == 1);
  REQUIRE(results[0]["id"] == "2");

  api.stop();
  th.join();
}

TEST_CASE("HTTP attrs on bulk index", "[integration][http][attrs][h1]") {
  hound::FuzzyIndex index;
  hound::HttpApi api(index);
  const int port = api.server().bind_to_any_port("127.0.0.1");
  REQUIRE(port > 0);
  std::thread th([&] { api.server().listen_after_bind(); });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  httplib::Client client("127.0.0.1", port);
  client.set_connection_timeout(1, 0);
  client.set_read_timeout(1, 0);

  auto bulk = client.Post(
      "/index/bulk",
      R"([
        {"id":"1","text":"Alpha","external_score":1,"attrs":{"tenant":"a"}},
        {"id":"2","text":"Alpha","external_score":2,"attrs":{"tenant":"b"}}
      ])",
      "application/json");
  REQUIRE(bulk);
  REQUIRE(bulk->status == 200);

  auto filtered = client.Get("/search?q=alpha&limit=10&attrs.tenant=a");
  REQUIRE(filtered);
  REQUIRE(filtered->status == 200);
  auto body = nlohmann::json::parse(filtered->body);
  REQUIRE(body["results"].size() == 1);
  REQUIRE(body["results"][0]["id"] == "1");
  REQUIRE_FALSE(body["results"][0].contains("attrs"));

  api.stop();
  th.join();
}
```

Verify bulk body is a **JSON array** (existing `/index/bulk` contract), not a
`{documents:…}` wrapper.

- [ ] **Step 4: Correctness**

```bash
./scripts/run_correctness.sh
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add include/hound/http_api.hpp tests/integration/test_http_api.cpp
git commit -m "$(cat <<'EOF'
feat(api): accept attrs on upsert and attrs.* search filters

EOF
)"
```

---

### Task 4: Snapshot v2 + bulk JSON attrs

**Files:**
- Modify: `include/hound/snapshot.hpp`
- Modify: `tests/unit/test_snapshot.cpp`
- Modify: `include/hound/bulk_loader.hpp`
- Modify: `tests/unit/test_bulk_loader.cpp`
- Modify: `docs/snapshot.md`

**Interfaces:**
- Produces: `kSnapshotVersion = 2`; reject other versions with rebuild message;
  JSON `--load` reads optional `attrs` object

- [ ] **Step 1: Bump snapshot write/read**

Set `kSnapshotVersion = 2`.

In `save_snapshot`, after `write_f64(…, doc.external_score)`:

```cpp
    detail::write_u32(out, static_cast<std::uint32_t>(doc.attrs.size()));
    for (const auto& [k, v] : doc.attrs) {
      detail::write_string(out, k);
      detail::write_string(out, v);
    }
```

In `load_snapshot`, replace version check message and after `external_score`:

```cpp
  if (version != kSnapshotVersion) {
    throw std::runtime_error(
        "snapshot: unsupported version (rebuild with --load / bulk; v1 not migrated)");
  }
  // ...
    doc.external_score = detail::read_f64(in);
    const auto attr_count = detail::read_u32(in);
    for (std::uint32_t a = 0; a < attr_count; ++a) {
      auto k = detail::read_string(in);
      auto v = detail::read_string(in);
      doc.attrs.emplace(std::move(k), std::move(v));
    }
    index.upsert(std::move(doc));
```

- [ ] **Step 2: Snapshot tests**

Add cases in `tests/unit/test_snapshot.cpp`:

```cpp
TEST_CASE("snapshot v2 roundtrips attrs", "[snapshot][attrs][h1]") {
  hound::FuzzyIndex idx;
  idx.upsert({"1", "Alpha", 1.5, {{"tenant", "a"}, {"open", "1"}}});
  const auto path = std::filesystem::temp_directory_path() / "hound_test_attrs.snap";
  hound::save_snapshot(idx, path.string());
  hound::FuzzyIndex loaded;
  hound::load_snapshot(loaded, path.string());
  hound::SearchOptions opt;
  opt.limit = 5;
  opt.attr_filters = {{"tenant", "a"}};
  auto hits = loaded.search("alpha", opt);
  REQUIRE(hits.size() == 1);
  REQUIRE(hits[0].id == "1");
  std::filesystem::remove(path);
}

TEST_CASE("snapshot v1 file is rejected", "[snapshot][attrs][h1]") {
  const auto path = std::filesystem::temp_directory_path() / "hound_test_v1.snap";
  {
    std::ofstream out(path, std::ios::binary);
    // Little-endian, same layout as detail::write_u32 / write_u64
    auto put_u32 = [&](std::uint32_t v) {
      out.put(static_cast<char>(v & 0xff));
      out.put(static_cast<char>((v >> 8) & 0xff));
      out.put(static_cast<char>((v >> 16) & 0xff));
      out.put(static_cast<char>((v >> 24) & 0xff));
    };
    auto put_u64 = [&](std::uint64_t v) {
      for (int i = 0; i < 8; ++i) {
        out.put(static_cast<char>((v >> (8 * i)) & 0xff));
      }
    };
    put_u32(0x484e4453);  // 'HNDS'
    put_u32(1);           // version 1
    put_u64(0);           // empty docs
  }
  hound::FuzzyIndex loaded;
  REQUIRE_THROWS_WITH(hound::load_snapshot(loaded, path.string()),
                      Catch::Matchers::ContainsSubstring("unsupported version"));
  std::filesystem::remove(path);
}

TEST_CASE("snapshot v2 roundtrips empty attrs map", "[snapshot][attrs][h1]") {
  hound::FuzzyIndex idx;
  idx.upsert({"1", "Alpha", 1.5, {}});
  const auto path = std::filesystem::temp_directory_path() / "hound_test_empty_attrs.snap";
  hound::save_snapshot(idx, path.string());
  hound::FuzzyIndex loaded;
  hound::load_snapshot(loaded, path.string());
  auto doc = loaded.get("1");
  REQUIRE(doc.has_value());
  REQUIRE(doc->attrs.empty());
  hound::SearchOptions opt{.limit = 5, .attr_filters = {{"tenant", "a"}}};
  REQUIRE(loaded.search("alpha", opt).empty());
  REQUIRE_FALSE(loaded.search("alpha", {.limit = 5}).empty());
  std::filesystem::remove(path);
}
```

Add `#include <fstream>` and
`#include <catch2/matchers/catch_matchers_string.hpp>` at the top of the test
file.

- [ ] **Step 3: JSON bulk loader attrs**

In `load_json_array` (`bulk_loader.hpp`), replace the unknown-key skip for
`attrs` with an object parse. After the `external_score` branch, add:

```cpp
      } else if (key == "attrs") {
        skip_ws(i);
        if (i >= content.size() || content[i] != '{') {
          throw std::runtime_error("bulk: attrs must be an object");
        }
        ++i;
        while (true) {
          skip_ws(i);
          if (i < content.size() && content[i] == '}') {
            ++i;
            break;
          }
          auto ak = parse_string(i);
          if (ak.empty()) {
            throw std::runtime_error("bulk: attrs key must be non-empty");
          }
          skip_ws(i);
          if (i >= content.size() || content[i] != ':') {
            throw std::runtime_error("bulk: expected ':'");
          }
          ++i;
          skip_ws(i);
          auto av = parse_string(i);
          doc.attrs.emplace(std::move(ak), std::move(av));
          skip_ws(i);
          if (i < content.size() && content[i] == ',') {
            ++i;
            continue;
          }
        }
```

Leave the generic unknown-key skip for other keys. CSV path unchanged
(attrs stay empty).

- [ ] **Step 4: Bulk loader tests**

In `tests/unit/test_bulk_loader.cpp`, write a temp `.json` file:

```json
[
  {"id":"1","text":"Alpha","external_score":1.0,"attrs":{"tenant":"a"}},
  {"id":"2","text":"Alpha","external_score":2.0,"attrs":{"tenant":"b"}}
]
```

Load with `load_json_array`, then `search("alpha", opt)` with
`attr_filters = {{"tenant","a"}}` → only id `"1"`.

Also add CSV case: load the existing CSV shape (`id,text,external_score` only);
assert docs are searchable unfiltered, and
`attr_filters = {{"tenant","a"}}` yields **no** hits (attrs stay empty).

```cpp
TEST_CASE("load CSV bulk leaves attrs empty", "[bulk][attrs][h1]") {
  const auto path = std::filesystem::temp_directory_path() / "hound_test_bulk_no_attrs.csv";
  {
    std::ofstream out(path);
    out << "id,text,external_score\n";
    out << "1,Ada Ash,10.5\n";
  }
  hound::FuzzyIndex idx;
  REQUIRE(hound::load_csv(idx, path.string()) == 1);
  REQUIRE_FALSE(idx.search("ada", {.limit = 5}).empty());
  hound::SearchOptions filtered{.limit = 5, .attr_filters = {{"tenant", "a"}}};
  REQUIRE(idx.search("ada", filtered).empty());
  auto doc = idx.get("1");
  REQUIRE(doc.has_value());
  REQUIRE(doc->attrs.empty());
  std::filesystem::remove(path);
}
```

- [ ] **Step 5: Update `docs/snapshot.md`**

Note version **2**, per-doc attrs, and that v1 requires rebuild via `--load` /
bulk (no in-process migration).

- [ ] **Step 6: Correctness + commit**

```bash
./scripts/run_correctness.sh
git add include/hound/snapshot.hpp include/hound/bulk_loader.hpp \
  tests/unit/test_snapshot.cpp tests/unit/test_bulk_loader.cpp docs/snapshot.md
git commit -m "$(cat <<'EOF'
feat(snapshot): version 2 stores document attrs

EOF
)"
```

---

### Task 5: Measure trade-offs (micro benches) — before docs claim “cheap”

**Files:**
- Modify: `benchmarks/micro/bench_ops.cpp`
- Modify: `docs/REFINEMENT.md` (Phase 2 changelog with **numbers**)

**Interfaces:**
- Produces: tracked micros for insert-with-attrs and filtered fuzzy search;
  changelog table with/without filter

- [ ] **Step 1: Add benches (synthetic only)**

In `benchmarks/micro/bench_ops.cpp`, add (names are gate-adjacent; keep old
gates unchanged):

```cpp
// BM_InsertWithAttrs/N — same as BM_Insert but each doc has attrs{{"tenant", id%64}}
// BM_SearchFuzzyFiltered/N/D — build N docs, 64 tenants, attr_filters tenant="0",
//   then search fuzzy with max_edit_distance D (same queries as BM_SearchFuzzy)
```

Register:

```cpp
BENCHMARK(BM_InsertWithAttrs)->Arg(20000)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_SearchFuzzyFiltered)
    ->Args({20000, 1})
    ->Args({20000, 2})
    ->Unit(benchmark::kMicrosecond);
```

- [ ] **Step 2: Run Release micro + compare baseline gates**

```bash
./scripts/run_micro.sh
./scripts/compare_bench.py baselines/micro_baseline.json benchmarks/results/micro_*.json
```

Capture in notes:

| Metric | Unfiltered / no attrs | With attrs / filtered |
|--------|----------------------|------------------------|
| `BM_Insert/20000` | (baseline) | — |
| `BM_InsertWithAttrs/20000` | — | (new) |
| `BM_SearchFuzzy/20000/2` | (gate) | — |
| `BM_SearchFuzzyFiltered/20000/2` | — | (new) |

Expected: **gate** metrics (old names) within +10% or justify. New benches have
no baseline yet — record absolute µs/ms in REFINEMENT only.

- [ ] **Step 3: Commit benches + measurement notes stub**

```bash
git add benchmarks/micro/bench_ops.cpp docs/REFINEMENT.md
git commit -m "$(cat <<'EOF'
bench: measure attrs insert and filtered fuzzy trade-offs

EOF
)"
```

---

### Task 6: Docs, OpenAPI, REFINEMENT status (after numbers exist)

**Files:**
- Modify: `docs/openapi.yaml`, `docs/compat.md`, `docs/search-params.md`,
  `docs/errors.md`, `docs/REFINEMENT.md`, `CHANGELOG.md`, `README.md`

**Interfaces:**
- Produces: documented additive MINOR surface matching Tasks 3–4; trade-off
  table filled from Task 5

- [ ] **Step 1: OpenAPI**

In `components.schemas.Document`, add optional:

```yaml
        attrs:
          type: object
          additionalProperties:
            type: string
          description: Opaque string metadata; replaced wholesale on upsert
```

Under `/search` parameters, document repeated pattern `attrs.<key>` (description
+ example); keep existing params unchanged.

- [ ] **Step 2: compat + search-params + errors**

- `compat.md`: Document JSON row adds optional `attrs`; search params add
  `attrs.*`; note MINOR.
- `search-params.md`: equality AND semantics; omit = no filter; missing key on
  doc = no match; **under-fetch** note + link to REFINEMENT numbers.
- `errors.md`: 400 cases for bad `attrs` body / empty `attrs.` key.

- [ ] **Step 3: REFINEMENT + CHANGELOG + README**

- Mark H1 attrs equality Done with measurement table (insert delta, filtered
  fuzzy vs unfiltered, under-fetch fixture outcome).
- Note string-only wire; flat routes; multi-index still next.
- Point H1 sketch at this spec (string attrs, not numeric).
- `CHANGELOG.md` `[Unreleased]`: additive attrs filters + honest trade-offs.
- README: one short example of upsert + filtered search; hydrate-in-app.

- [ ] **Step 4: Final correctness + commit**

```bash
./scripts/run_correctness.sh
git add docs/openapi.yaml docs/compat.md docs/search-params.md docs/errors.md \
  docs/REFINEMENT.md CHANGELOG.md README.md
git commit -m "$(cat <<'EOF'
docs: document attrs equality filters and measured trade-offs (H1)

EOF
)"
```

---

### Task 7: Smoke checklist (human)

- [ ] `curl` upsert with attrs + filtered search against local `./build/hound`
- [ ] Confirm `/search` without `attrs.*` matches pre-change behavior on sample
- [ ] Skim REFINEMENT H1 numbers; decide if eligible-first follow-up is needed
- [ ] Do **not** tag release until multi-index plan exists or explicit v0.2.0 decision

(Wipe-on-omit-attrs is covered by Catch2 in Task 3 — manual curl optional.)

---

## Spec coverage (self-review)

| Spec requirement | Task |
|------------------|------|
| `Document::attrs` map | 1 (RED) |
| Postings + upsert/erase/clear + publish-swap copy | 1–2 |
| AND equality filter before rank | 1–2 |
| Wipe on empty/omit attrs; empty-string value | 1 + 3 |
| Rank only within eligible | 1 |
| Under-fetch trade-off fixture | 1–2 |
| E3 deferred attrs + PublishSwap prepare postings | 1 |
| HTTP `attrs` + `attrs.*` + 400s + last-wins + bulk | 3 |
| Hits omit `attrs` | 3 |
| Snapshot v2 + empty attrs + reject v1 | 4 |
| JSON bulk attrs; CSV omit (empty attrs) | 4 |
| Micro: insert+attrs, filtered fuzzy, gate vs baseline | **5** |
| OpenAPI / compat / REFINEMENT with **numbers** | **6** |
| Multi-index | **Out** (slice 2) |

## Slice 2 reminder (do not implement in this plan)

Multi-index hybrid (`IndexRegistry`, `/indexes/{name}`, flat = `default`) gets its
own spec + plan after H1 is merged and micro-accepted.
