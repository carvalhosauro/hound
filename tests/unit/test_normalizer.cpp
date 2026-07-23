#include <catch2/catch_test_macros.hpp>

#include "hound/normalizer.hpp"

TEST_CASE("normalize lowercases ASCII", "[normalizer]") {
  REQUIRE(hound::normalize("Hello World") == "hello world");
}

TEST_CASE("normalize drops non-ASCII bytes", "[normalizer]") {
  REQUIRE(hound::normalize("cafe\xc3\xa9") == "cafe");
}

TEST_CASE("normalize collapses whitespace", "[normalizer]") {
  REQUIRE(hound::normalize("  Foo   Bar  ") == "foo bar");
}
