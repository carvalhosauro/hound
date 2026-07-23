#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "hound/bk_tree.hpp"
#include "hound/fuzzy_backend.hpp"

namespace hound {

// SymSpell-style symmetric-delete index (edits ≤ max_dictionary_edit_distance).
// Lookup generates query deletes, unions candidate dictionary words, then verifies
// with Levenshtein. Full hit-parity vs BK is expected for max_distance ≤ 2.
class SymSpellFuzzyBackend final : public FuzzyBackend {
 public:
  explicit SymSpellFuzzyBackend(int max_dictionary_edit_distance = 2)
      : max_dict_edits_(std::max(0, max_dictionary_edit_distance)) {}

  void insert(std::string key, std::string id) override {
    auto& ids = dictionary_[key];
    if (std::find(ids.begin(), ids.end(), id) == ids.end()) {
      ids.push_back(std::move(id));
    }
    if (ids.size() == 1) {
      index_deletes_for(key);
    }
  }

  void erase(std::string_view key, const std::string& id) override {
    const std::string key_str(key);
    auto it = dictionary_.find(key_str);
    if (it == dictionary_.end()) {
      return;
    }
    auto& ids = it->second;
    ids.erase(std::remove(ids.begin(), ids.end(), id), ids.end());
    if (!ids.empty()) {
      return;
    }
    unindex_deletes_for(key_str);
    dictionary_.erase(it);
  }

  std::vector<FuzzyMatch> search(std::string_view query, int max_distance) const override {
    std::vector<FuzzyMatch> out;
    if (max_distance < 0) {
      return out;
    }
    const int verify_distance = max_distance;
    const int gen_edits = std::min(max_distance, max_dict_edits_);

    std::unordered_set<std::string> candidates;
    auto consider_word = [&](const std::string& word) {
      if (dictionary_.find(word) != dictionary_.end()) {
        candidates.insert(word);
      }
    };

    consider_word(std::string(query));

    std::unordered_set<std::string> query_deletes;
    generate_deletes(std::string(query), gen_edits, query_deletes);
    for (const auto& del : query_deletes) {
      consider_word(del);
      auto dit = deletes_.find(del);
      if (dit == deletes_.end()) {
        continue;
      }
      for (const auto& word : dit->second) {
        candidates.insert(word);
      }
    }

    auto qit = deletes_.find(std::string(query));
    if (qit != deletes_.end()) {
      for (const auto& word : qit->second) {
        candidates.insert(word);
      }
    }

    for (const auto& word : candidates) {
      const int dist = levenshtein(word, query);
      if (dist > verify_distance) {
        continue;
      }
      auto dit = dictionary_.find(word);
      if (dit == dictionary_.end() || dit->second.empty()) {
        continue;
      }
      out.push_back(FuzzyMatch{word, dit->second, dist});
    }
    return out;
  }

  void clear() override {
    dictionary_.clear();
    deletes_.clear();
  }

  int max_dictionary_edit_distance() const { return max_dict_edits_; }

 private:
  void index_deletes_for(const std::string& word) {
    std::unordered_set<std::string> dels;
    generate_deletes(word, max_dict_edits_, dels);
    for (const auto& del : dels) {
      deletes_[del].insert(word);
    }
  }

  void unindex_deletes_for(const std::string& word) {
    std::unordered_set<std::string> dels;
    generate_deletes(word, max_dict_edits_, dels);
    for (const auto& del : dels) {
      auto it = deletes_.find(del);
      if (it == deletes_.end()) {
        continue;
      }
      it->second.erase(word);
      if (it->second.empty()) {
        deletes_.erase(it);
      }
    }
  }

  // All strings obtained by deleting 1..max_edits characters (iterative BFS).
  // Does not include the original word.
  static void generate_deletes(const std::string& word, int max_edits,
                               std::unordered_set<std::string>& out) {
    if (max_edits <= 0 || word.empty()) {
      return;
    }
    std::unordered_set<std::string> frontier{word};
    for (int edit = 1; edit <= max_edits; ++edit) {
      std::unordered_set<std::string> next;
      for (const auto& w : frontier) {
        for (std::size_t i = 0; i < w.size(); ++i) {
          std::string del = w;
          del.erase(i, 1);
          if (out.insert(del).second) {
            next.insert(std::move(del));
          }
        }
      }
      frontier = std::move(next);
      if (frontier.empty()) {
        break;
      }
    }
  }

  int max_dict_edits_;
  std::unordered_map<std::string, std::vector<std::string>> dictionary_;
  std::unordered_map<std::string, std::unordered_set<std::string>> deletes_;
};

inline std::unique_ptr<FuzzyBackend> make_fuzzy_backend(
    FuzzyBackendKind kind = default_fuzzy_backend_kind()) {
  switch (kind) {
    case FuzzyBackendKind::SymSpell:
      return std::make_unique<SymSpellFuzzyBackend>();
    case FuzzyBackendKind::BkTree:
    default:
      return std::make_unique<BkFuzzyBackend>();
  }
}

inline std::unique_ptr<FuzzyBackend> make_default_fuzzy_backend() {
  return make_fuzzy_backend(default_fuzzy_backend_kind());
}

// Runtime feature flag (B3): HOUND_FUZZY_BACKEND=symspell|bk (default bk / compile default).
inline bool parse_fuzzy_backend_kind(std::string_view text, FuzzyBackendKind& out) {
  if (text == "symspell" || text == "SymSpell" || text == "symmetric_delete") {
    out = FuzzyBackendKind::SymSpell;
    return true;
  }
  if (text == "bk" || text == "BK" || text == "bktree" || text == "BkTree") {
    out = FuzzyBackendKind::BkTree;
    return true;
  }
  return false;
}

inline FuzzyBackendKind fuzzy_backend_kind_from_env() {
  const char* raw = std::getenv("HOUND_FUZZY_BACKEND");
  if (raw == nullptr || raw[0] == '\0') {
    return default_fuzzy_backend_kind();
  }
  FuzzyBackendKind kind = default_fuzzy_backend_kind();
  if (!parse_fuzzy_backend_kind(raw, kind)) {
    return default_fuzzy_backend_kind();
  }
  return kind;
}

}  // namespace hound
