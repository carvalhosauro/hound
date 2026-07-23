#include <catch2/catch_test_macros.hpp>

#include "hound/score_merger.hpp"

TEST_CASE("score merger respects alpha", "[score_merger]") {
  hound::ScoreMerger merger{{1.0}};
  std::vector<hound::SearchHit> hits = {
      {.id = "a", .text_relevance = 1.0, .external_score = 0.0},
      {.id = "b", .text_relevance = 0.5, .external_score = 100.0},
  };
  auto ranked = merger.merge(hits);
  REQUIRE(ranked.front().id == "a");

  merger.set_alpha(0.0);
  ranked = merger.merge(hits);
  REQUIRE(ranked.front().id == "b");
}

TEST_CASE("score merger balanced alpha order", "[score_merger]") {
  hound::ScoreMerger merger{{0.5}};
  // Equal text relevance → external score decides after min-max norm.
  std::vector<hound::SearchHit> hits = {
      {.id = "low", .text_relevance = 0.8, .external_score = 10.0},
      {.id = "high", .text_relevance = 0.8, .external_score = 90.0},
  };
  auto ranked = merger.merge(hits);
  REQUIRE(ranked.size() == 2);
  REQUIRE(ranked.front().id == "high");
  REQUIRE(ranked.back().id == "low");
}

TEST_CASE("score merger empty input", "[score_merger]") {
  hound::ScoreMerger merger;
  auto ranked = merger.merge({});
  REQUIRE(ranked.empty());
}

TEST_CASE("score merger tie breaks by id", "[score_merger]") {
  hound::ScoreMerger merger{{1.0}};
  std::vector<hound::SearchHit> hits = {
      {.id = "b", .text_relevance = 1.0, .external_score = 1.0},
      {.id = "a", .text_relevance = 1.0, .external_score = 1.0},
  };
  auto ranked = merger.merge(hits);
  REQUIRE(ranked.front().id == "a");
  REQUIRE(ranked.back().id == "b");
}

TEST_CASE("distance_to_relevance", "[score_merger]") {
  REQUIRE(hound::distance_to_relevance(0, 2) > hound::distance_to_relevance(1, 2));
  REQUIRE(hound::distance_to_relevance(3, 2) == 0.0);
}
