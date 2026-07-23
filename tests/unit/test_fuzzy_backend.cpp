#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <set>
#include <string>
#include <vector>

#include "hound/bk_tree.hpp"
#include "hound/fuzzy_backend.hpp"
#include "hound/fuzzy_index.hpp"

namespace {

std::set<std::string> ids_from_bk(const hound::BkTree& tree, std::string_view q, int d) {
  std::set<std::string> out;
  for (const auto& m : tree.search(q, d)) {
    for (const auto& id : m.ids) {
      out.insert(id);
    }
  }
  return out;
}

std::set<std::string> ids_from_backend(const hound::FuzzyBackend& backend, std::string_view q,
                                       int d) {
  std::set<std::string> out;
  for (const auto& m : backend.search(q, d)) {
    for (const auto& id : m.ids) {
      out.insert(id);
    }
  }
  return out;
}

}  // namespace

TEST_CASE("BkFuzzyBackend matches BkTree hits on fixture", "[fuzzy_backend][b1]") {
  hound::BkTree tree;
  hound::BkFuzzyBackend backend;

  const std::vector<std::pair<std::string, std::string>> rows = {
      {"alpha", "1"},
      {"alpine", "2"},
      {"beta", "3"},
      {"alphabet", "4"},
  };
  for (const auto& [key, id] : rows) {
    tree.insert(key, id);
    backend.insert(key, id);
  }

  SECTION("exact and fuzzy queries agree") {
    for (const auto& [q, dist] : {std::pair{"alpha", 0}, {"alpga", 2}, {"zzzzz", 1}}) {
      REQUIRE(ids_from_backend(backend, q, dist) == ids_from_bk(tree, q, dist));
    }
  }

  SECTION("erase hides id for both") {
    tree.erase("alpha", "1");
    backend.erase("alpha", "1");
    REQUIRE(ids_from_backend(backend, "alpha", 0) == ids_from_bk(tree, "alpha", 0));
    REQUIRE(ids_from_backend(backend, "alpha", 0).count("1") == 0);
  }

  SECTION("clear empties backend") {
    backend.clear();
    REQUIRE(backend.search("alpha", 2).empty());
  }
}

TEST_CASE("default FuzzyIndex fuzzy path uses BK backend parity", "[fuzzy_backend][b1]") {
  // Same normalized keys the index would store; proves default backend is BK-equivalent.
  hound::BkTree oracle;
  oracle.insert("alpha ridge", "1");
  oracle.insert("alpine lake", "2");

  hound::FuzzyIndex idx;  // default ctor — must keep BK behavior
  idx.upsert({"1", "Alpha Ridge", 10.0});
  idx.upsert({"2", "Alpine Lake", 5.0});

  auto typo = idx.search("alpga ridge", {.limit = 10, .alpha = 1.0, .max_edit_distance = 2});
  auto oracle_ids = ids_from_bk(oracle, "alpga ridge", 2);

  REQUIRE_FALSE(typo.empty());
  REQUIRE(oracle_ids.count("1") == 1);

  bool found = false;
  for (const auto& h : typo) {
    if (h.id == "1") {
      found = true;
    }
  }
  REQUIRE(found);
}

TEST_CASE("FuzzyIndex accepts injected FuzzyBackend", "[fuzzy_backend][b1]") {
  auto backend = std::make_unique<hound::BkFuzzyBackend>();
  auto* raw = backend.get();
  hound::FuzzyIndex idx(std::move(backend));

  idx.upsert({"1", "Catalog", 1.0});
  REQUIRE_FALSE(raw->search("catalog", 0).empty());

  idx.erase("1");
  REQUIRE(raw->search("catalog", 0).empty());

  idx.clear();
  REQUIRE(raw->search("catalog", 2).empty());
}
