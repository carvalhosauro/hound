#include <catch2/catch_test_macros.hpp>

#include "hound/trie.hpp"

TEST_CASE("trie insert and contains", "[trie]") {
  hound::Trie t;
  t.insert("alpha", "1");
  t.insert("alpine", "2");
  REQUIRE(t.contains("alpha"));
  REQUIRE(t.contains("alpine"));
  REQUIRE_FALSE(t.contains("alp"));
}

TEST_CASE("trie completions by prefix", "[trie]") {
  hound::Trie t;
  t.insert("cat", "a");
  t.insert("car", "b");
  t.insert("dog", "c");
  auto comps = t.completions("ca", 10);
  REQUIRE(comps.size() == 2);
  REQUIRE(comps[0].first == "car");
  REQUIRE(comps[1].first == "cat");
}

TEST_CASE("trie erase removes id", "[trie]") {
  hound::Trie t;
  t.insert("same", "1");
  t.insert("same", "2");
  t.erase("same", "1");
  auto comps = t.completions("same", 10);
  REQUIRE(comps.size() == 1);
  REQUIRE(comps[0].second.count("2") == 1);
  REQUIRE(comps[0].second.count("1") == 0);
  t.erase("same", "2");
  REQUIRE_FALSE(t.contains("same"));
}
