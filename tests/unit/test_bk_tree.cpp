#include <catch2/catch_test_macros.hpp>

#include "hound/bk_tree.hpp"

TEST_CASE("levenshtein basic", "[bk_tree]") {
  REQUIRE(hound::levenshtein("kitten", "sitting") == 3);
  REQUIRE(hound::levenshtein("abc", "abc") == 0);
  REQUIRE(hound::levenshtein("", "ab") == 2);
}

TEST_CASE("bk-tree finds within distance", "[bk_tree]") {
  hound::BkTree tree;
  tree.insert("alpha", "1");
  tree.insert("alpine", "2");
  tree.insert("beta", "3");

  auto hits = tree.search("alpga", 2);
  REQUIRE_FALSE(hits.empty());
  bool found_alpha = false;
  for (const auto& m : hits) {
    if (m.key == "alpha") {
      found_alpha = true;
      REQUIRE(m.distance <= 2);
    }
  }
  REQUIRE(found_alpha);
}

TEST_CASE("bk-tree erase hides id", "[bk_tree]") {
  hound::BkTree tree;
  tree.insert("word", "1");
  tree.erase("word", "1");
  auto hits = tree.search("word", 0);
  REQUIRE(hits.empty());
}
