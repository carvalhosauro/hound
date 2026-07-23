#include <catch2/catch_test_macros.hpp>

#include "hound/fuzzy_index.hpp"

TEST_CASE("fuzzy index upsert search delete", "[fuzzy_index]") {
  hound::FuzzyIndex idx;
  idx.upsert({"1", "Alpha Ridge", 10.0});
  idx.upsert({"2", "Alpine Lake", 5.0});
  idx.upsert({"3", "Beta Hill", 1.0});

  auto exact = idx.search("alpha ridge", {.limit = 5});
  REQUIRE_FALSE(exact.empty());
  REQUIRE(exact.front().id == "1");

  auto typo = idx.search("alpga ridge", {.limit = 5, .max_edit_distance = 2});
  REQUIRE_FALSE(typo.empty());
  bool found = false;
  for (const auto& h : typo) {
    if (h.id == "1") {
      found = true;
    }
  }
  REQUIRE(found);

  REQUIRE(idx.erase("1"));
  auto after = idx.search("alpha ridge", {.limit = 5});
  for (const auto& h : after) {
    REQUIRE(h.id != "1");
  }
}

TEST_CASE("fuzzy index upsert replaces text", "[fuzzy_index]") {
  hound::FuzzyIndex idx;
  idx.upsert({"1", "Old Name", 1.0});
  idx.upsert({"1", "New Name", 2.0});
  REQUIRE(idx.size() == 1);
  auto hits = idx.search("new name", {.limit = 5});
  REQUIRE_FALSE(hits.empty());
  REQUIRE(hits.front().id == "1");
  auto old = idx.search("old name", {.limit = 5, .max_edit_distance = 0});
  // With distance 0 and no shared prefix path that equals old, may be empty.
  bool has_old_exact = false;
  for (const auto& h : old) {
    if (h.id == "1" && h.text_relevance >= 0.99) {
      has_old_exact = true;
    }
  }
  REQUIRE_FALSE(has_old_exact);
}
