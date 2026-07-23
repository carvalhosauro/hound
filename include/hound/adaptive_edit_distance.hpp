#pragma once

#include <cstddef>
#include <optional>

namespace hound {

// Phase C — Sonic-style adaptive max edit distance from normalized query length.
//
// | normalized length | max_edit_distance |
// |-------------------|-------------------|
// | 0 .. 2            | 0                 |
// | 3 .. 5            | 1                 |
// | 6+                | 2                 |
//
// Rationale: short tokens explode candidate sets under d≥1; long tokens need
// room for typos. Cap stays at 2 to match SymSpell dictionary depth / Phase B.
inline int adaptive_max_edit_distance(std::size_t normalized_query_len) {
  if (normalized_query_len <= 2) {
    return 0;
  }
  if (normalized_query_len <= 5) {
    return 1;
  }
  return 2;
}

// Optional explicit override (C2 wires SearchOptions); nullopt → adaptive table.
inline int resolve_max_edit_distance(std::size_t normalized_query_len,
                                     std::optional<int> override_distance) {
  if (override_distance.has_value()) {
    return *override_distance;
  }
  return adaptive_max_edit_distance(normalized_query_len);
}

}  // namespace hound
