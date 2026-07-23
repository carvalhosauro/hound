#include <catch2/catch_test_macros.hpp>

#include <filesystem>

#include "hound/fuzzy_index.hpp"
#include "hound/snapshot.hpp"

TEST_CASE("snapshot roundtrip", "[snapshot]") {
  hound::FuzzyIndex a;
  a.upsert({"1", "Drew Dale", 12.0});
  a.upsert({"2", "Eden Elm", 4.0});

  const auto path = std::filesystem::temp_directory_path() / "hound_test.snap";
  hound::save_snapshot(a, path.string());

  hound::FuzzyIndex b;
  hound::load_snapshot(b, path.string());
  REQUIRE(b.size() == 2);
  auto hits = b.search("drew dale", {.limit = 5});
  REQUIRE_FALSE(hits.empty());
  REQUIRE(hits.front().id == "1");
  std::filesystem::remove(path);
}
