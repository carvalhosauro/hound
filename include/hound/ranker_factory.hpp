#pragma once

#include <cstdlib>
#include <memory>
#include <string_view>

#include "hound/ranker.hpp"
#include "hound/score_merger.hpp"
#include "hound/tie_break_ranker.hpp"

namespace hound {

inline std::unique_ptr<Ranker> make_ranker(RankerKind kind = default_ranker_kind()) {
  switch (kind) {
    case RankerKind::TieBreak:
      return make_tie_break_ranker();
    case RankerKind::Linear:
    default:
      return make_default_ranker();
  }
}

// Accepts: linear | score_merger | tie_break | tiebreak (case-sensitive ASCII).
inline bool parse_ranker_kind(std::string_view text, RankerKind& out) {
  if (text == "linear" || text == "score_merger") {
    out = RankerKind::Linear;
    return true;
  }
  if (text == "tie_break" || text == "tiebreak") {
    out = RankerKind::TieBreak;
    return true;
  }
  return false;
}

inline const char* ranker_kind_name(RankerKind kind) {
  switch (kind) {
    case RankerKind::TieBreak:
      return "tie_break";
    case RankerKind::Linear:
    default:
      return "linear";
  }
}

// HOUND_RANKER=linear|tie_break (invalid/missing → compile default).
inline RankerKind ranker_kind_from_env() {
  const char* raw = std::getenv("HOUND_RANKER");
  if (raw == nullptr || raw[0] == '\0') {
    return default_ranker_kind();
  }
  RankerKind kind = default_ranker_kind();
  if (!parse_ranker_kind(raw, kind)) {
    return default_ranker_kind();
  }
  return kind;
}

}  // namespace hound
