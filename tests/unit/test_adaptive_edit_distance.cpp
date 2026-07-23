#include <catch2/catch_test_macros.hpp>

#include "hound/adaptive_edit_distance.hpp"

TEST_CASE("adaptive_max_edit_distance table (C1)", "[adaptive][c1]") {
  // Empty / tiny: no fuzzy (avoids short-token explosions).
  REQUIRE(hound::adaptive_max_edit_distance(0) == 0);
  REQUIRE(hound::adaptive_max_edit_distance(1) == 0);
  REQUIRE(hound::adaptive_max_edit_distance(2) == 0);

  // Short: single edit.
  REQUIRE(hound::adaptive_max_edit_distance(3) == 1);
  REQUIRE(hound::adaptive_max_edit_distance(4) == 1);
  REQUIRE(hound::adaptive_max_edit_distance(5) == 1);

  // Medium / long: up to 2 (SymSpell / historical default cap).
  REQUIRE(hound::adaptive_max_edit_distance(6) == 2);
  REQUIRE(hound::adaptive_max_edit_distance(10) == 2);
  REQUIRE(hound::adaptive_max_edit_distance(64) == 2);
}

TEST_CASE("resolve_max_edit_distance honors explicit override", "[adaptive][c1]") {
  REQUIRE(hound::resolve_max_edit_distance(1, /*override=*/2) == 2);
  REQUIRE(hound::resolve_max_edit_distance(10, /*override=*/0) == 0);
  REQUIRE(hound::resolve_max_edit_distance(10, /*override=*/std::nullopt) == 2);
  REQUIRE(hound::resolve_max_edit_distance(3, /*override=*/std::nullopt) == 1);
}
