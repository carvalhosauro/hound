#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "hound/document.hpp"
#include "hound/ranker.hpp"

namespace hound {

struct ScoreMergerConfig {
  double alpha = 0.7;  // weight on text relevance
};

// Default Ranker: linear α·text + (1-α)·norm(external), then score desc / id asc.
class ScoreMerger final : public Ranker {
 public:
  explicit ScoreMerger(ScoreMergerConfig config = {}) : config_(config) {}

  void set_alpha(double alpha) {
    config_.alpha = std::clamp(alpha, 0.0, 1.0);
  }

  double alpha() const { return config_.alpha; }

  // Ranker API — uses RankOptions::alpha (clamped). Preferred for shared Ranker.
  std::vector<SearchHit> rank(std::vector<SearchHit> candidates,
                              RankOptions opt = {}) const override {
    return merge_with_alpha(std::move(candidates), opt.alpha);
  }

  // Convenience: uses ScoreMergerConfig::alpha (standalone / unit tests).
  std::vector<SearchHit> merge(std::vector<SearchHit> candidates) const {
    return merge_with_alpha(std::move(candidates), config_.alpha);
  }

 private:
  // Candidates carry raw text relevance in [0,1] and external scores.
  // External scores are min-max normalized within the candidate set.
  std::vector<SearchHit> merge_with_alpha(std::vector<SearchHit> candidates,
                                          double alpha) const {
    if (candidates.empty()) {
      return candidates;
    }

    alpha = std::clamp(alpha, 0.0, 1.0);

    double min_ext = std::numeric_limits<double>::infinity();
    double max_ext = -std::numeric_limits<double>::infinity();
    for (const auto& c : candidates) {
      min_ext = std::min(min_ext, c.external_score);
      max_ext = std::max(max_ext, c.external_score);
    }
    const double span = max_ext - min_ext;

    for (auto& c : candidates) {
      const double text = std::clamp(c.text_relevance, 0.0, 1.0);
      const double ext_norm =
          (span <= 0.0) ? 1.0 : (c.external_score - min_ext) / span;
      c.score = alpha * text + (1.0 - alpha) * ext_norm;
    }

    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const SearchHit& a, const SearchHit& b) {
                       if (a.score != b.score) {
                         return a.score > b.score;
                       }
                       return a.id < b.id;
                     });
    return candidates;
  }

  ScoreMergerConfig config_;
};

inline std::unique_ptr<Ranker> make_default_ranker() {
  return std::make_unique<ScoreMerger>();
}

// Convert edit distance to text relevance in [0,1].
inline double distance_to_relevance(int distance, int max_distance) {
  if (max_distance <= 0) {
    return distance == 0 ? 1.0 : 0.0;
  }
  if (distance > max_distance) {
    return 0.0;
  }
  return 1.0 - (static_cast<double>(distance) / static_cast<double>(max_distance + 1));
}

}  // namespace hound
