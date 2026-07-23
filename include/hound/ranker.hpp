#pragma once

#include <vector>

#include "hound/document.hpp"

namespace hound {

// Per-call knobs for Ranker::rank. Concrete rankers may ignore fields they
// do not use (e.g. TieBreakRanker ignores alpha).
struct RankOptions {
  double alpha = 0.7;  // text vs external blend weight
};

// Runtime / CLI / HTTP selection. Default = Linear (ScoreMerger).
enum class RankerKind {
  Linear,   // α blend — ScoreMerger
  TieBreak  // text → external → id — TieBreakRanker
};

inline constexpr RankerKind kCompileDefaultRanker = RankerKind::Linear;

inline RankerKind default_ranker_kind() { return kCompileDefaultRanker; }

// Pluggable ranking. Production default is ScoreMerger (linear α blend).
// alpha is passed per call so a shared Ranker stays safe under concurrent search.
class Ranker {
 public:
  virtual ~Ranker() = default;

  virtual std::vector<SearchHit> rank(std::vector<SearchHit> candidates,
                                      RankOptions opt = {}) const = 0;
};

}  // namespace hound
