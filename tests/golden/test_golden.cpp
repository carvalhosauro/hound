#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "hound/bulk_loader.hpp"
#include "hound/fuzzy_index.hpp"
#include "hound/symspell_backend.hpp"

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

TEST_CASE("golden SymSpell FuzzyIndex matches BK hit ids (d=2)", "[golden][symspell][b2]") {
  hound::FuzzyIndex bk_index(hound::make_fuzzy_backend(hound::FuzzyBackendKind::BkTree));
  hound::FuzzyIndex sym_index(hound::make_fuzzy_backend(hound::FuzzyBackendKind::SymSpell));
  REQUIRE(hound::load_csv(bk_index, golden_path("tests/golden/corpus.csv")) == 10);
  REQUIRE(hound::load_csv(sym_index, golden_path("tests/golden/corpus.csv")) == 10);

  auto cases = load_cases(golden_path("tests/golden/cases.csv"));
  REQUIRE_FALSE(cases.empty());

  for (const auto& c : cases) {
    auto bk_hits = bk_index.search(c.query, {.limit = c.k, .max_edit_distance = 2});
    auto sym_hits = sym_index.search(c.query, {.limit = c.k, .max_edit_distance = 2});

    std::set<std::string> bk_ids;
    std::set<std::string> sym_ids;
    for (const auto& h : bk_hits) {
      bk_ids.insert(h.id);
    }
    for (const auto& h : sym_hits) {
      sym_ids.insert(h.id);
    }
    INFO("query=" << c.query);
    REQUIRE(sym_ids == bk_ids);
  }
}
