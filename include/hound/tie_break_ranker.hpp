#pragma once

#include <algorithm>
#include <memory>
#include <vector>

#include "hound/document.hpp"
#include "hound/ranker.hpp"

namespace hound {

// Typesense-style lexicographic ranking (optional; default remains ScoreMerger):
//   1) text_relevance desc  (~ _text_match)
//   2) external_score desc  (~ default_sorting_field / popularity)
//   3) id asc               (deterministic final tie-break)
//
// RankOptions::alpha is ignored. hit.score is set to text_relevance (primary key)
// so the JSON `score` field reflects the primary sort criterion; external_score
// remains available for clients that want the tie-break value.
class TieBreakRanker final : public Ranker {
 public:
  std::vector<SearchHit> rank(std::vector<SearchHit> candidates,
                              RankOptions /*opt*/ = {}) const override {
    if (candidates.empty()) {
      return candidates;
    }

    for (auto& c : candidates) {
      c.text_relevance = std::clamp(c.text_relevance, 0.0, 1.0);
      c.score = c.text_relevance;
    }

    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const SearchHit& a, const SearchHit& b) {
                       if (a.text_relevance != b.text_relevance) {
                         return a.text_relevance > b.text_relevance;
                       }
                       if (a.external_score != b.external_score) {
                         return a.external_score > b.external_score;
                       }
                       return a.id < b.id;
                     });
    return candidates;
  }
};

inline std::unique_ptr<Ranker> make_tie_break_ranker() {
  return std::make_unique<TieBreakRanker>();
}

}  // namespace hound
