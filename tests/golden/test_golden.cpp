#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "hound/bulk_loader.hpp"
#include "hound/fuzzy_index.hpp"

namespace {

struct GoldenCase {
  std::string query;
  std::string expected_id;
  std::size_t k = 10;
};

std::string golden_path(const char* relative) {
  // HOUND_SOURCE_DIR is set by CMake to the project root.
  return std::string(HOUND_SOURCE_DIR) + "/" + relative;
}

std::vector<GoldenCase> load_cases(const std::string& path) {
  std::ifstream in(path);
  REQUIRE(in.good());
  std::string line;
  REQUIRE(std::getline(in, line));  // header
  std::vector<GoldenCase> cases;
  while (std::getline(in, line)) {
    if (line.empty()) {
      continue;
    }
    std::stringstream ss(line);
    GoldenCase c;
    std::string k_str;
    std::string note;
    REQUIRE(std::getline(ss, c.query, ','));
    REQUIRE(std::getline(ss, c.expected_id, ','));
    REQUIRE(std::getline(ss, k_str, ','));
    c.k = static_cast<std::size_t>(std::stoul(k_str));
    cases.push_back(std::move(c));
  }
  return cases;
}

}  // namespace

TEST_CASE("golden dataset recall@k and MRR", "[golden]") {
  hound::FuzzyIndex index;
  const auto n = hound::load_csv(index, golden_path("tests/golden/corpus.csv"));
  REQUIRE(n == 10);

  auto cases = load_cases(golden_path("tests/golden/cases.csv"));
  REQUIRE_FALSE(cases.empty());

  double hits = 0.0;
  double mrr_sum = 0.0;
  for (const auto& c : cases) {
    auto results = index.search(c.query, {.limit = c.k, .max_edit_distance = 2});
    std::size_t rank = 0;
    bool found = false;
    for (std::size_t i = 0; i < results.size(); ++i) {
      if (results[i].id == c.expected_id) {
        found = true;
        rank = i + 1;
        break;
      }
    }
    if (found) {
      hits += 1.0;
      mrr_sum += 1.0 / static_cast<double>(rank);
    }
  }

  const double recall_at_k = hits / static_cast<double>(cases.size());
  const double mrr = mrr_sum / static_cast<double>(cases.size());

  // Printed for humans / CI logs (not just pass/fail).
  WARN("golden recall@k=" << recall_at_k << " MRR=" << mrr << " n=" << cases.size());

  // Controlled corpus: expect near-perfect recall.
  REQUIRE(recall_at_k >= 0.95);
  REQUIRE(mrr >= 0.80);
}
