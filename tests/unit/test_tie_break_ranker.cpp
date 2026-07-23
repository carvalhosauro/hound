#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>

#include "hound/fuzzy_index.hpp"
#include "hound/score_merger.hpp"
#include "hound/tie_break_ranker.hpp"

namespace {

// Documented ranking fixture for Phase D2.
// Same text_relevance; external_score breaks the tie under TieBreakRanker.
// ScoreMerger(alpha=1.0) ignores external and falls back to id asc.
const std::vector<hound::SearchHit> kEqualTextFixture = {
    {.id = "c-low", .text_relevance = 0.8, .external_score = 10.0},
    {.id = "a-mid", .text_relevance = 0.8, .external_score = 50.0},
    {.id = "b-high", .text_relevance = 0.8, .external_score = 90.0},
};

// Higher text always wins under TieBreak, even with lower external.
// ScoreMerger with low alpha can invert this via blended score.
const std::vector<hound::SearchHit> kTextVsExternalFixture = {
    {.id = "weak-text-hot", .text_relevance = 0.4, .external_score = 100.0},
    {.id = "strong-text-cold", .text_relevance = 0.9, .external_score = 1.0},
};

std::vector<std::string> ids_of(const std::vector<hound::SearchHit>& hits) {
  std::vector<std::string> out;
  out.reserve(hits.size());
  for (const auto& h : hits) {
    out.push_back(h.id);
  }
  return out;
}

}  // namespace

TEST_CASE("tie-break fixture: equal text → external desc then id", "[ranker][tie_break][d2]") {
  // Expected order (Typesense-style):
  //   text_relevance desc → external_score desc → id asc
  // All share text=0.8, so: b-high (90) > a-mid (50) > c-low (10).
  hound::TieBreakRanker ranker;
  auto ranked = ranker.rank(kEqualTextFixture);

  REQUIRE(ids_of(ranked) == std::vector<std::string>{"b-high", "a-mid", "c-low"});
  for (const auto& h : ranked) {
    REQUIRE(h.score == h.text_relevance);
    REQUIRE(h.score == 0.8);
  }
}

TEST_CASE("tie-break fixture: higher text beats higher external", "[ranker][tie_break][d2]") {
  hound::TieBreakRanker ranker;
  auto ranked = ranker.rank(kTextVsExternalFixture);

  REQUIRE(ids_of(ranked) ==
          std::vector<std::string>{"strong-text-cold", "weak-text-hot"});
}

TEST_CASE("tie-break final id asc when text and external tie", "[ranker][tie_break][d2]") {
  hound::TieBreakRanker ranker;
  std::vector<hound::SearchHit> hits = {
      {.id = "z", .text_relevance = 1.0, .external_score = 5.0},
      {.id = "a", .text_relevance = 1.0, .external_score = 5.0},
      {.id = "m", .text_relevance = 1.0, .external_score = 5.0},
  };
  auto ranked = ranker.rank(std::move(hits));
  REQUIRE(ids_of(ranked) == std::vector<std::string>{"a", "m", "z"});
}

TEST_CASE("tie-break ignores RankOptions::alpha", "[ranker][tie_break][d2]") {
  hound::TieBreakRanker ranker;
  auto with_zero = ranker.rank(kEqualTextFixture, hound::RankOptions{0.0});
  auto with_one = ranker.rank(kEqualTextFixture, hound::RankOptions{1.0});
  REQUIRE(ids_of(with_zero) == ids_of(with_one));
  REQUIRE(ids_of(with_zero) == std::vector<std::string>{"b-high", "a-mid", "c-low"});
}

TEST_CASE("default ScoreMerger differs from TieBreak on equal-text fixture",
          "[ranker][tie_break][d2]") {
  // alpha=1.0 → score = text only → all scores equal → id asc (not external).
  hound::ScoreMerger linear{{1.0}};
  auto linear_order = ids_of(linear.merge(kEqualTextFixture));
  REQUIRE(linear_order == std::vector<std::string>{"a-mid", "b-high", "c-low"});

  hound::TieBreakRanker tie;
  auto tie_order = ids_of(tie.rank(kEqualTextFixture));
  REQUIRE(tie_order == std::vector<std::string>{"b-high", "a-mid", "c-low"});
  REQUIRE(tie_order != linear_order);
}

TEST_CASE("ScoreMerger low alpha can invert TieBreak text-primary order",
          "[ranker][tie_break][d2]") {
  hound::TieBreakRanker tie;
  REQUIRE(ids_of(tie.rank(kTextVsExternalFixture)).front() == "strong-text-cold");

  // alpha=0 → pure external after min-max → weak-text-hot wins.
  hound::ScoreMerger linear{{0.0}};
  REQUIRE(ids_of(linear.merge(kTextVsExternalFixture)).front() == "weak-text-hot");
}

TEST_CASE("make_default_ranker is still ScoreMerger (D2 opt-in only)", "[ranker][d2]") {
  auto ranker = hound::make_default_ranker();
  REQUIRE(dynamic_cast<hound::ScoreMerger*>(ranker.get()) != nullptr);
  REQUIRE(dynamic_cast<hound::TieBreakRanker*>(ranker.get()) == nullptr);
}

TEST_CASE("FuzzyIndex with TieBreakRanker uses external as text-tie break",
          "[fuzzy_index][tie_break][d2]") {
  hound::FuzzyIndex idx(hound::make_default_fuzzy_backend(),
                        hound::make_tie_break_ranker());
  // Same normalized text → same text_relevance; external decides order.
  idx.upsert({"cold", "Same Text", 1.0});
  idx.upsert({"hot", "Same Text", 100.0});

  auto hits = idx.search("same text", {.limit = 10, .alpha = 1.0, .max_edit_distance = 0});
  REQUIRE(hits.size() == 2);
  REQUIRE(hits.front().id == "hot");
  REQUIRE(hits.back().id == "cold");
}

TEST_CASE("FuzzyIndex default still blends via ScoreMerger", "[fuzzy_index][d2]") {
  hound::FuzzyIndex idx;
  idx.upsert({"cold", "Same Text", 1.0});
  idx.upsert({"hot", "Same Text", 100.0});

  // alpha=1.0 → equal scores → id asc: "cold" before "hot".
  auto hits = idx.search("same text", {.limit = 10, .alpha = 1.0, .max_edit_distance = 0});
  REQUIRE(hits.size() == 2);
  REQUIRE(hits.front().id == "cold");
  REQUIRE(hits.back().id == "hot");
}
