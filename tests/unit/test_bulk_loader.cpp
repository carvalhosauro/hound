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

TEST_CASE("load JSON bulk with attrs filters", "[bulk][attrs][h1]") {
  const auto path = std::filesystem::temp_directory_path() / "hound_test_bulk_attrs.json";
  {
    std::ofstream out(path);
    out << R"([
  {"id":"1","text":"Alpha","external_score":1.0,"attrs":{"tenant":"a"}},
  {"id":"2","text":"Alpha","external_score":2.0,"attrs":{"tenant":"b"}}
])";
  }
  hound::FuzzyIndex idx;
  REQUIRE(hound::load_json_array(idx, path.string()) == 2);
  hound::SearchOptions opt{.limit = 5, .attr_filters = {{"tenant", "a"}}};
  auto hits = idx.search("alpha", opt);
  REQUIRE(hits.size() == 1);
  REQUIRE(hits[0].id == "1");
  std::filesystem::remove(path);
}

TEST_CASE("load CSV bulk leaves attrs empty", "[bulk][attrs][h1]") {
  const auto path = std::filesystem::temp_directory_path() / "hound_test_bulk_no_attrs.csv";
  {
    std::ofstream out(path);
    out << "id,text,external_score\n";
    out << "1,Ada Ash,10.5\n";
  }
  hound::FuzzyIndex idx;
  REQUIRE(hound::load_csv(idx, path.string()) == 1);
  REQUIRE_FALSE(idx.search("ada", {.limit = 5}).empty());
  hound::SearchOptions filtered{.limit = 5, .attr_filters = {{"tenant", "a"}}};
  REQUIRE(idx.search("ada", filtered).empty());
  auto doc = idx.get("1");
  REQUIRE(doc.has_value());
  REQUIRE(doc->attrs.empty());
  std::filesystem::remove(path);
}
