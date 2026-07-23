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
  bool has_old_exact = false;
  for (const auto& h : old) {
    if (h.id == "1" && h.text_relevance >= 0.99) {
      has_old_exact = true;
    }
  }
  REQUIRE_FALSE(has_old_exact);
}

TEST_CASE("fuzzy index empty returns no hits", "[fuzzy_index]") {
  hound::FuzzyIndex idx;
  auto hits = idx.search("anything", {.limit = 10});
  REQUIRE(hits.empty());
}

TEST_CASE("fuzzy index single-char query", "[fuzzy_index]") {
  hound::FuzzyIndex idx;
  idx.upsert({"1", "Alpha", 1.0});
  idx.upsert({"2", "Beta", 1.0});
  auto hits = idx.search("a", {.limit = 10, .max_edit_distance = 1});
  REQUIRE_FALSE(hits.empty());
}

TEST_CASE("fuzzy index prefix autocomplete", "[fuzzy_index]") {
  hound::FuzzyIndex idx;
  idx.upsert({"1", "Catalog", 1.0});
  idx.upsert({"2", "Category", 1.0});
  idx.upsert({"3", "Dog", 1.0});
  auto hits = idx.search("cat", {.limit = 10, .max_edit_distance = 0});
  REQUIRE(hits.size() >= 2);
  bool saw1 = false;
  bool saw2 = false;
  for (const auto& h : hits) {
    if (h.id == "1") {
      saw1 = true;
    }
    if (h.id == "2") {
      saw2 = true;
    }
    REQUIRE(h.id != "3");
  }
  REQUIRE(saw1);
  REQUIRE(saw2);
}

TEST_CASE("fuzzy index beyond edit distance is not a hit", "[fuzzy_index]") {
  hound::FuzzyIndex idx;
  idx.upsert({"1", "Alpha", 1.0});
  // "zzzzz" is far from "alpha"
  auto hits = idx.search("zzzzz", {.limit = 10, .max_edit_distance = 1});
  for (const auto& h : hits) {
    REQUIRE(h.id != "1");
  }
}

TEST_CASE("fuzzy index unicode bytes dropped by normalizer", "[fuzzy_index]") {
  hound::FuzzyIndex idx;
  // MVP: non-ASCII bytes stripped — "cafe" + combining bytes → "cafe"
  idx.upsert({"1", "cafe\xc3\xa9", 1.0});
  auto hits = idx.search("cafe", {.limit = 5});
  REQUIRE_FALSE(hits.empty());
  REQUIRE(hits.front().id == "1");
}

TEST_CASE("fuzzy index erase missing id", "[fuzzy_index]") {
  hound::FuzzyIndex idx;
  REQUIRE_FALSE(idx.erase("missing"));
}

TEST_CASE("fuzzy index duplicate id upsert", "[fuzzy_index]") {
  hound::FuzzyIndex idx;
  idx.upsert({"1", "First", 1.0});
  idx.upsert({"1", "First", 9.0});
  REQUIRE(idx.size() == 1);
  auto doc = idx.get("1");
  REQUIRE(doc.has_value());
  REQUIRE(doc->external_score == 9.0);
}

TEST_CASE("fuzzy index adaptive distance suppresses tiny-query typos", "[fuzzy_index][adaptive][c2]") {
  hound::FuzzyIndex idx;
  idx.upsert({"1", "ab", 1.0});  // normalized len 2 → adaptive d=0
  // Typo at distance 1 should not match under adaptive default.
  auto hits = idx.search("ax", {.limit = 10});
  for (const auto& h : hits) {
    REQUIRE(h.id != "1");
  }
  // Explicit override restores fixed d=1 behavior.
  auto forced = idx.search("ax", {.limit = 10, .max_edit_distance = 1});
  bool found = false;
  for (const auto& h : forced) {
    if (h.id == "1") {
      found = true;
    }
  }
  REQUIRE(found);
}

TEST_CASE("fuzzy index adaptive distance allows medium typos", "[fuzzy_index][adaptive][c2]") {
  hound::FuzzyIndex idx;
  idx.upsert({"1", "Alpha Ridge", 1.0});  // normalized longer than 5 → d=2
  auto hits = idx.search("alpga ridge", {.limit = 10});
  bool found = false;
  for (const auto& h : hits) {
    if (h.id == "1") {
      found = true;
    }
  }
  REQUIRE(found);
}
