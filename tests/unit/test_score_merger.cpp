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

TEST_CASE("distance_to_relevance", "[score_merger]") {
  REQUIRE(hound::distance_to_relevance(0, 2) > hound::distance_to_relevance(1, 2));
  REQUIRE(hound::distance_to_relevance(3, 2) == 0.0);
}
