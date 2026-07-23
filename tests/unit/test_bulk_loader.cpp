#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#include "hound/bulk_loader.hpp"
#include "hound/fuzzy_index.hpp"

TEST_CASE("load CSV bulk", "[bulk]") {
  const auto path = std::filesystem::temp_directory_path() / "hound_test_bulk.csv";
  {
    std::ofstream out(path);
    out << "id,text,external_score\n";
    out << "1,Ada Ash,10.5\n";
    out << "2,Blake Brook,3.0\n";
  }
  hound::FuzzyIndex idx;
  const auto n = hound::load_csv(idx, path.string());
  REQUIRE(n == 2);
  REQUIRE(idx.size() == 2);
  auto hits = idx.search("ada ash", {.limit = 5});
  REQUIRE_FALSE(hits.empty());
  REQUIRE(hits.front().id == "1");
  std::filesystem::remove(path);
}

TEST_CASE("load JSON bulk", "[bulk]") {
  const auto path = std::filesystem::temp_directory_path() / "hound_test_bulk.json";
  {
    std::ofstream out(path);
    out << R"([{"id":"9","text":"Casey Cedar","external_score":7.0}])";
  }
  hound::FuzzyIndex idx;
  const auto n = hound::load_json_array(idx, path.string());
  REQUIRE(n == 1);
  auto hits = idx.search("casey", {.limit = 5});
  REQUIRE_FALSE(hits.empty());
  REQUIRE(hits.front().id == "9");
  std::filesystem::remove(path);
}
