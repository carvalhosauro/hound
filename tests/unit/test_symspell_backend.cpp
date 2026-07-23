#include <catch2/catch_test_macros.hpp>

#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "hound/fuzzy_backend.hpp"
#include "hound/fuzzy_index.hpp"
#include "hound/symspell_backend.hpp"

namespace {

std::set<std::string> ids_of(const hound::FuzzyBackend& backend, std::string_view q, int d) {
  std::set<std::string> out;
  for (const auto& m : backend.search(q, d)) {
    for (const auto& id : m.ids) {
      out.insert(id);
    }
  }
  return out;
}

void fill_fixture(hound::FuzzyBackend& backend) {
  const std::vector<std::pair<std::string, std::string>> rows = {
      {"alpha", "1"},
      {"alpine", "2"},
      {"beta", "3"},
      {"alphabet", "4"},
      {"catalog", "5"},
      {"category", "6"},
  };
  for (const auto& [key, id] : rows) {
    backend.insert(key, id);
  }
}

}  // namespace

TEST_CASE("SymSpellFuzzyBackend matches BkFuzzyBackend hits on fixture (d<=2)",
          "[symspell][b2]") {
  hound::BkFuzzyBackend bk;
  hound::SymSpellFuzzyBackend sym;
  fill_fixture(bk);
  fill_fixture(sym);

  const std::vector<std::pair<std::string, int>> queries = {
      {"alpha", 0},
      {"alpha", 1},
      {"alpha", 2},
      {"alpga", 1},
      {"alpga", 2},
      {"alpin", 1},
      {"alpin", 2},
      {"bete", 1},
      {"zzzzz", 1},
      {"zzzzz", 2},
      {"catalog", 0},
      {"catlog", 1},
      {"catlog", 2},
  };

  for (const auto& [q, d] : queries) {
    INFO("query=" << q << " d=" << d);
    REQUIRE(ids_of(sym, q, d) == ids_of(bk, q, d));
  }
}

TEST_CASE("SymSpellFuzzyBackend erase and clear", "[symspell][b2]") {
  hound::SymSpellFuzzyBackend sym;
  sym.insert("alpha", "1");
  sym.insert("alpha", "2");
  REQUIRE(ids_of(sym, "alpha", 0).count("1") == 1);
  REQUIRE(ids_of(sym, "alpha", 0).count("2") == 1);

  sym.erase("alpha", "1");
  REQUIRE(ids_of(sym, "alpha", 0).count("1") == 0);
  REQUIRE(ids_of(sym, "alpha", 0).count("2") == 1);

  sym.erase("alpha", "2");
  REQUIRE(ids_of(sym, "alpha", 0).empty());
  REQUIRE(ids_of(sym, "alpga", 2).count("2") == 0);

  sym.insert("beta", "3");
  sym.clear();
  REQUIRE(sym.search("beta", 2).empty());
}

TEST_CASE("make_fuzzy_backend flag selects SymSpell; default remains BK", "[symspell][b2]") {
  auto def = hound::make_fuzzy_backend();
  auto bk = hound::make_fuzzy_backend(hound::FuzzyBackendKind::BkTree);
  auto sym = hound::make_fuzzy_backend(hound::FuzzyBackendKind::SymSpell);

  REQUIRE(dynamic_cast<hound::BkFuzzyBackend*>(def.get()) != nullptr);
  REQUIRE(dynamic_cast<hound::BkFuzzyBackend*>(bk.get()) != nullptr);
  REQUIRE(dynamic_cast<hound::SymSpellFuzzyBackend*>(sym.get()) != nullptr);

  // Default factory used by FuzzyIndex stays BK (compile/runtime default).
  REQUIRE(hound::default_fuzzy_backend_kind() == hound::FuzzyBackendKind::BkTree);
}

TEST_CASE("parse_fuzzy_backend_kind accepts bk and symspell", "[symspell][b2]") {
  hound::FuzzyBackendKind kind = hound::FuzzyBackendKind::BkTree;
  REQUIRE(hound::parse_fuzzy_backend_kind("symspell", kind));
  REQUIRE(kind == hound::FuzzyBackendKind::SymSpell);
  REQUIRE(hound::parse_fuzzy_backend_kind("bk", kind));
  REQUIRE(kind == hound::FuzzyBackendKind::BkTree);
  REQUIRE_FALSE(hound::parse_fuzzy_backend_kind("nope", kind));
}

TEST_CASE("FuzzyIndex with SymSpell backend finds golden typos", "[symspell][b2][b3]") {
  hound::FuzzyIndex idx(hound::make_fuzzy_backend(hound::FuzzyBackendKind::SymSpell));
  idx.upsert({"g1", "Alpha Ridge", 10.0});
  idx.upsert({"g2", "Alpine Lake", 5.0});

  auto typo = idx.search("alpga ridge", {.limit = 10, .max_edit_distance = 2});
  bool found = false;
  for (const auto& h : typo) {
    if (h.id == "g1") {
      found = true;
    }
  }
  REQUIRE(found);
}
