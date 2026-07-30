#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <thread>

#include "hound/fuzzy_index.hpp"

TEST_CASE("attrs filter AND equality", "[attrs][h1]") {
  hound::FuzzyIndex index;
  index.upsert({"1", "Queijo Mussarela", 1.0, {{"tenant", "a"}}});
  index.upsert({"2", "Queijo Mussarela", 9.0, {{"tenant", "b"}}});
  hound::SearchOptions opt;
  opt.limit = 10;
  opt.attr_filters = {{"tenant", "a"}};
  auto hits = index.search("queijo", opt);
  REQUIRE(hits.size() == 1);
  REQUIRE(hits[0].id == "1");
}

TEST_CASE("attrs omit filter keeps both", "[attrs][h1]") {
  hound::FuzzyIndex index;
  index.upsert({"1", "Queijo Mussarela", 1.0, {{"tenant", "a"}}});
  index.upsert({"2", "Queijo Mussarela", 9.0, {{"tenant", "b"}}});
  auto hits = index.search("queijo", {.limit = 10});
  REQUIRE(hits.size() == 2);
  REQUIRE(hits[0].id == "2");  // higher external_score
  REQUIRE(hits[1].id == "1");
}

TEST_CASE("attrs missing key excludes doc", "[attrs][h1]") {
  hound::FuzzyIndex index;
  index.upsert({"1", "Queijo", 1.0, {{"tenant", "a"}}});
  index.upsert({"3", "Queijo", 9.0, {}});  // no attrs
  hound::SearchOptions opt;
  opt.limit = 10;
  opt.attr_filters = {{"tenant", "a"}};
  auto hits = index.search("queijo", opt);
  REQUIRE(hits.size() == 1);
  REQUIRE(hits[0].id == "1");
}

TEST_CASE("attrs AND requires all keys", "[attrs][h1]") {
  hound::FuzzyIndex index;
  index.upsert({"1", "Item", 1.0, {{"tenant", "a"}, {"open", "1"}}});
  index.upsert({"2", "Item", 9.0, {{"tenant", "a"}, {"open", "0"}}});
  hound::SearchOptions opt;
  opt.limit = 10;
  opt.attr_filters = {{"tenant", "a"}, {"open", "1"}};
  auto hits = index.search("item", opt);
  REQUIRE(hits.size() == 1);
  REQUIRE(hits[0].id == "1");
}

TEST_CASE("attrs upsert replaces map wholesale", "[attrs][h1]") {
  hound::FuzzyIndex index;
  index.upsert({"1", "Item", 1.0, {{"tenant", "a"}}});
  index.upsert({"1", "Item", 1.0, {{"tenant", "b"}}});
  hound::SearchOptions opt_a{.limit = 10, .attr_filters = {{"tenant", "a"}}};
  hound::SearchOptions opt_b{.limit = 10, .attr_filters = {{"tenant", "b"}}};
  REQUIRE(index.search("item", opt_a).empty());
  auto hits = index.search("item", opt_b);
  REQUIRE(hits.size() == 1);
  REQUIRE(hits[0].id == "1");
}

TEST_CASE("attrs erase removes from postings", "[attrs][h1]") {
  hound::FuzzyIndex index;
  index.upsert({"1", "Item", 1.0, {{"tenant", "a"}}});
  REQUIRE(index.erase("1"));
  hound::SearchOptions opt{.limit = 10, .attr_filters = {{"tenant", "a"}}};
  REQUIRE(index.search("item", opt).empty());
}

TEST_CASE("attrs unknown filter value yields no hits", "[attrs][h1]") {
  hound::FuzzyIndex index;
  index.upsert({"1", "Item", 1.0, {{"tenant", "a"}}});
  hound::SearchOptions opt{.limit = 10, .attr_filters = {{"tenant", "z"}}};
  REQUIRE(index.search("item", opt).empty());
}

TEST_CASE("attrs under-fetch: low limit can miss eligible after intersect", "[attrs][h1][tradeoff]") {
  // Many docs share the same text; only the last tenant is eligible.
  // With a tiny limit, global gather may not include the eligible id → empty.
  // Documents the intersect trade-off (callers: raise limit or wait for eligible-first).
  hound::FuzzyIndex index;
  for (int i = 0; i < 32; ++i) {
    index.upsert({std::to_string(i), "Shared Label", static_cast<double>(i),
                  {{"tenant", std::string(1, static_cast<char>('a' + (i % 26)))}}});
  }
  index.upsert({"eligible", "Shared Label", 0.0, {{"tenant", "zz"}}});
  hound::SearchOptions tight{.limit = 1, .attr_filters = {{"tenant", "zz"}}};
  hound::SearchOptions wide{.limit = 64, .attr_filters = {{"tenant", "zz"}}};
  // Wide must find eligible; tight may be empty — record both outcomes in assert comments
  // after GREEN: REQUIRE wide hits; INFO/CAPTURE tight size for changelog numbers.
  auto wide_hits = index.search("shared", wide);
  REQUIRE(wide_hits.size() == 1);
  REQUIRE(wide_hits[0].id == "eligible");
  auto tight_hits = index.search("shared", tight);
  // Soft assert: document actual size in CAPTURE (0 is the known risk).
  CAPTURE(tight_hits.size());
  SUCCEED("trade-off probe: tight_hits=" << tight_hits.size());
}

// --- High priority: wipe, empty value, filter-before-rank ---

TEST_CASE("attrs empty map on upsert wipes prior attrs", "[attrs][h1]") {
  hound::FuzzyIndex index;
  index.upsert({"1", "Item", 1.0, {{"tenant", "a"}}});
  index.upsert({"1", "Item", 1.0, {}});  // empty map = wipe
  hound::SearchOptions opt{.limit = 10, .attr_filters = {{"tenant", "a"}}};
  REQUIRE(index.search("item", opt).empty());
  auto doc = index.get("1");
  REQUIRE(doc.has_value());
  REQUIRE(doc->attrs.empty());
}

TEST_CASE("attrs empty string value is filterable", "[attrs][h1]") {
  hound::FuzzyIndex index;
  index.upsert({"1", "Item", 1.0, {{"open", ""}}});
  index.upsert({"2", "Item", 9.0, {{"open", "1"}}});
  hound::SearchOptions empty_v{.limit = 10, .attr_filters = {{"open", ""}}};
  hound::SearchOptions one{.limit = 10, .attr_filters = {{"open", "1"}}};
  auto hits_empty = index.search("item", empty_v);
  REQUIRE(hits_empty.size() == 1);
  REQUIRE(hits_empty[0].id == "1");
  auto hits_one = index.search("item", one);
  REQUIRE(hits_one.size() == 1);
  REQUIRE(hits_one[0].id == "2");
}

TEST_CASE("attrs filter ranks only within eligible set", "[attrs][h1]") {
  // Other tenant has higher external_score; must not win under filter.
  hound::FuzzyIndex index;
  index.upsert({"low", "Shared", 1.0, {{"tenant", "a"}}});
  index.upsert({"high", "Shared", 5.0, {{"tenant", "a"}}});
  index.upsert({"other", "Shared", 99.0, {{"tenant", "b"}}});
  hound::SearchOptions opt{.limit = 10, .attr_filters = {{"tenant", "a"}}};
  auto hits = index.search("shared", opt);
  REQUIRE(hits.size() == 2);
  REQUIRE(hits[0].id == "high");
  REQUIRE(hits[1].id == "low");
}

// --- Medium priority: clear, publish-swap copy, E3 deferred ---

TEST_CASE("attrs clear removes postings (legacy)", "[attrs][h1]") {
  hound::FuzzyIndex index;
  index.upsert({"1", "Item", 1.0, {{"tenant", "a"}}});
  index.clear();
  hound::SearchOptions opt{.limit = 10, .attr_filters = {{"tenant", "a"}}};
  REQUIRE(index.search("item", opt).empty());
  REQUIRE(index.size() == 0);
}

TEST_CASE("attrs clear removes postings (publish-swap)", "[attrs][h1][publish_swap]") {
  hound::FuzzyIndex index(hound::make_default_fuzzy_backend(), hound::make_default_ranker(),
                          hound::PublishMode::PublishSwap);
  index.upsert({"1", "Item", 1.0, {{"tenant", "a"}}});
  index.prepare();
  index.clear();
  hound::SearchOptions opt{.limit = 10, .attr_filters = {{"tenant", "a"}}};
  REQUIRE(index.search("item", opt).empty());
  REQUIRE(index.size() == 0);
}

TEST_CASE("attrs publish-swap copy keeps postings after prepare", "[attrs][h1][publish_swap]") {
  // PublishSwap copies IndexState; attr_postings must be included or filters break.
  hound::FuzzyIndex index(hound::make_default_fuzzy_backend(), hound::make_default_ranker(),
                          hound::PublishMode::PublishSwap);
  index.begin_bulk();
  index.upsert({"1", "Alpha Ridge", 10.0, {{"tenant", "a"}}});
  index.upsert({"2", "Alpha Ridge", 20.0, {{"tenant", "b"}}});
  REQUIRE(index.search("alpha", {.limit = 10, .attr_filters = {{"tenant", "a"}}}).empty());
  index.prepare();
  auto hits = index.search("alpha", {.limit = 10, .attr_filters = {{"tenant", "a"}}});
  REQUIRE(hits.size() == 1);
  REQUIRE(hits[0].id == "1");
}

TEST_CASE("attrs e3 deferred upsert not visible to filter until prepare", "[attrs][h1][e3]") {
  using namespace std::chrono_literals;
  hound::FuzzyIndex index(hound::make_default_fuzzy_backend(), hound::make_default_ranker(),
                          hound::PublishMode::PublishSwap, 500ms);
  index.upsert({"1", "Alpha Ridge", 10.0, {{"tenant", "a"}}});
  hound::SearchOptions opt{.limit = 10, .attr_filters = {{"tenant", "a"}}};
  REQUIRE(index.search("alpha", opt).empty());
  index.prepare();
  auto hits = index.search("alpha", opt);
  REQUIRE(hits.size() == 1);
  REQUIRE(hits[0].id == "1");
}
