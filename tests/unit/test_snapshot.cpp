#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <fstream>

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

TEST_CASE("snapshot v2 roundtrips attrs", "[snapshot][attrs][h1]") {
  hound::FuzzyIndex idx;
  idx.upsert({"1", "Alpha", 1.5, {{"tenant", "a"}, {"open", "1"}}});
  const auto path = std::filesystem::temp_directory_path() / "hound_test_attrs.snap";
  hound::save_snapshot(idx, path.string());
  hound::FuzzyIndex loaded;
  hound::load_snapshot(loaded, path.string());
  hound::SearchOptions opt;
  opt.limit = 5;
  opt.attr_filters = {{"tenant", "a"}};
  auto hits = loaded.search("alpha", opt);
  REQUIRE(hits.size() == 1);
  REQUIRE(hits[0].id == "1");
  std::filesystem::remove(path);
}

TEST_CASE("snapshot v1 file is rejected", "[snapshot][attrs][h1]") {
  const auto path = std::filesystem::temp_directory_path() / "hound_test_v1.snap";
  {
    std::ofstream out(path, std::ios::binary);
    auto put_u32 = [&](std::uint32_t v) {
      out.put(static_cast<char>(v & 0xff));
      out.put(static_cast<char>((v >> 8) & 0xff));
      out.put(static_cast<char>((v >> 16) & 0xff));
      out.put(static_cast<char>((v >> 24) & 0xff));
    };
    auto put_u64 = [&](std::uint64_t v) {
      for (int i = 0; i < 8; ++i) {
        out.put(static_cast<char>((v >> (8 * i)) & 0xff));
      }
    };
    put_u32(0x484e4453);
    put_u32(1);
    put_u64(0);
  }
  hound::FuzzyIndex loaded;
  REQUIRE_THROWS_WITH(hound::load_snapshot(loaded, path.string()),
                      Catch::Matchers::ContainsSubstring("unsupported version"));
  std::filesystem::remove(path);
}

TEST_CASE("snapshot v2 roundtrips empty attrs map", "[snapshot][attrs][h1]") {
  hound::FuzzyIndex idx;
  idx.upsert({"1", "Alpha", 1.5, {}});
  const auto path = std::filesystem::temp_directory_path() / "hound_test_empty_attrs.snap";
  hound::save_snapshot(idx, path.string());
  hound::FuzzyIndex loaded;
  hound::load_snapshot(loaded, path.string());
  auto doc = loaded.get("1");
  REQUIRE(doc.has_value());
  REQUIRE(doc->attrs.empty());
  hound::SearchOptions opt{.limit = 5, .attr_filters = {{"tenant", "a"}}};
  REQUIRE(loaded.search("alpha", opt).empty());
  REQUIRE_FALSE(loaded.search("alpha", {.limit = 5}).empty());
  std::filesystem::remove(path);
}
