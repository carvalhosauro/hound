#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "hound/bk_tree.hpp"
#include "hound/bk_fuzzy_backend.hpp"
#include "hound/fuzzy_backend.hpp"

namespace hound {

// SymSpell-style symmetric-delete index (edits ≤ max_dictionary_edit_distance).
// Dictionary updates are cheap; the delete map is rebuilt lazily on search/prepare
// (B4) so bulk ingest stays viable while lookup stays fast.
class SymSpellFuzzyBackend final : public FuzzyBackend {
 public:
  explicit SymSpellFuzzyBackend(int max_dictionary_edit_distance = 2)
      : max_dict_edits_(std::max(0, max_dictionary_edit_distance)) {}

  void insert(std::string key, std::string id) override {
    auto& ids = dictionary_[key];
    if (std::find(ids.begin(), ids.end(), id) == ids.end()) {
      ids.push_back(std::move(id));
    }
    mark_deletes_dirty();
  }

  void erase(std::string_view key, const std::string& id) override {
    const std::string key_str(key);
    auto it = dictionary_.find(key_str);
    if (it == dictionary_.end()) {
      return;
    }
    auto& ids = it->second;
    ids.erase(std::remove(ids.begin(), ids.end(), id), ids.end());
    if (ids.empty()) {
      dictionary_.erase(it);
    }
    mark_deletes_dirty();
  }

  std::vector<FuzzyMatch> search(std::string_view query, int max_distance) const override {
    ensure_deletes_built();
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

    std::vector<std::string> query_deletes;
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
    std::lock_guard lock(rebuild_mu_);
    dictionary_.clear();
    deletes_.clear();
    deletes_ready_ = true;
  }

  // Build delete index now (no-op if already current). Safe to call after bulk load.
  void prepare() override { ensure_deletes_built(); }

  int max_dictionary_edit_distance() const { return max_dict_edits_; }

 private:
  void mark_deletes_dirty() {
    std::lock_guard lock(rebuild_mu_);
    deletes_ready_ = false;
    deletes_.clear();
  }

  void ensure_deletes_built() const {
    std::lock_guard lock(rebuild_mu_);
    if (deletes_ready_) {
      return;
    }
    deletes_.clear();
    // Rough reserve: each word contributes O(len^2) deletes at distance 2.
    deletes_.reserve(dictionary_.size() * 8);
    for (const auto& [word, ids] : dictionary_) {
      if (ids.empty()) {
        continue;
      }
      index_deletes_for(word);
    }
    deletes_ready_ = true;
  }

  void index_deletes_for(const std::string& word) const {
    std::vector<std::string> dels;
    generate_deletes(word, max_dict_edits_, dels);
    for (auto& del : dels) {
      deletes_[std::move(del)].push_back(word);
    }
  }

  // Deletes at edit distance 1..max_edits (not including the original word).
  // Specialized for the practical SymSpell depths 1 and 2.
  static void generate_deletes(const std::string& word, int max_edits,
                               std::vector<std::string>& out) {
    if (max_edits <= 0 || word.empty()) {
      return;
    }
    const std::size_t n = word.size();
    out.reserve(out.size() + n + (max_edits >= 2 ? (n * (n - 1)) / 2 : 0));

    for (std::size_t i = 0; i < n; ++i) {
      std::string del;
      del.reserve(n - 1);
      del.append(word.data(), i);
      del.append(word.data() + i + 1, n - i - 1);
      out.push_back(std::move(del));
    }

    if (max_edits < 2 || n < 2) {
      return;
    }
    for (std::size_t i = 0; i < n; ++i) {
      for (std::size_t j = i + 1; j < n; ++j) {
        std::string del;
        del.reserve(n - 2);
        del.append(word.data(), i);
        del.append(word.data() + i + 1, j - i - 1);
        del.append(word.data() + j + 1, n - j - 1);
        out.push_back(std::move(del));
      }
    }
  }

  int max_dict_edits_;
  std::unordered_map<std::string, std::vector<std::string>> dictionary_;
  mutable std::unordered_map<std::string, std::vector<std::string>> deletes_;
  mutable bool deletes_ready_ = true;
  mutable std::mutex rebuild_mu_;
};

inline std::unique_ptr<FuzzyBackend> make_fuzzy_backend(
    FuzzyBackendKind kind = default_fuzzy_backend_kind()) {
  switch (kind) {
    case FuzzyBackendKind::BkTree:
      // Oracle / escape hatch only — not the default hot path (B5).
      return std::make_unique<BkFuzzyBackend>();
    case FuzzyBackendKind::SymSpell:
    default:
      return std::make_unique<SymSpellFuzzyBackend>();
  }
}

inline std::unique_ptr<FuzzyBackend> make_default_fuzzy_backend() {
  return make_fuzzy_backend(default_fuzzy_backend_kind());
}

// Runtime feature flag: HOUND_FUZZY_BACKEND=symspell|bk
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
