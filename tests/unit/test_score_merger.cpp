#include <catch2/catch_test_macros.hpp>

#include <memory>

#include "hound/ranker.hpp"
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

TEST_CASE("ranker interface score parity with ScoreMerger", "[ranker]") {
  const std::vector<hound::SearchHit> hits = {
      {.id = "a", .text_relevance = 1.0, .external_score = 0.0},
      {.id = "b", .text_relevance = 0.5, .external_score = 100.0},
      {.id = "c", .text_relevance = 0.9, .external_score = 50.0},
  };

  hound::ScoreMerger direct{{0.7}};
  auto via_merge = direct.merge(hits);

  std::unique_ptr<hound::Ranker> ranker = hound::make_default_ranker();
  auto via_rank = ranker->rank(hits, hound::RankOptions{0.7});

  REQUIRE(via_rank.size() == via_merge.size());
  for (std::size_t i = 0; i < via_merge.size(); ++i) {
    REQUIRE(via_rank[i].id == via_merge[i].id);
    REQUIRE(via_rank[i].score == via_merge[i].score);
    REQUIRE(via_rank[i].text_relevance == via_merge[i].text_relevance);
    REQUIRE(via_rank[i].external_score == via_merge[i].external_score);
  }
}

TEST_CASE("ranker RankOptions alpha overrides config", "[ranker]") {
  // Config alpha=1.0 would prefer text; RankOptions alpha=0.0 prefers external.
  hound::ScoreMerger merger{{1.0}};
  const std::vector<hound::SearchHit> hits = {
      {.id = "texty", .text_relevance = 1.0, .external_score = 0.0},
      {.id = "exty", .text_relevance = 0.1, .external_score = 100.0},
  };

  auto via_merge = merger.merge(hits);
  REQUIRE(via_merge.front().id == "texty");

  const hound::Ranker& ranker = merger;
  auto via_rank = ranker.rank(hits, hound::RankOptions{0.0});
  REQUIRE(via_rank.front().id == "exty");
}
