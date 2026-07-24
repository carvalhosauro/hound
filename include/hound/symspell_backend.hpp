#pragma once

#include <algorithm>
#include <cstdint>
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
//
// Issue #1: posting lists use uint32 word ids. Map value is either a single
// word id (MSB clear) or an index into multi_postings_ (MSB set) so the common
// 1-hit delete keys avoid vector overhead while search stays one lookup.
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

    std::unordered_set<std::uint32_t> candidates;
    auto consider_word_id = [&](std::uint32_t wid) {
      if (wid < words_by_id_.size()) {
        candidates.insert(wid);
      }
    };
    auto consider_word = [&](const std::string& word) {
      auto wit = word_to_id_.find(word);
      if (wit != word_to_id_.end()) {
        candidates.insert(wit->second);
      }
    };
    auto collect_deletes = [&](const std::string& key) {
      auto dit = deletes_.find(key);
      if (dit == deletes_.end()) {
        return;
      }
      const std::uint32_t ref = dit->second;
      if ((ref & kMultiFlag) == 0) {
        consider_word_id(ref);
        return;
      }
      const std::uint32_t idx = ref & kMultiMask;
      for (const std::uint32_t wid : multi_postings_[idx]) {
        consider_word_id(wid);
      }
    };

    consider_word(std::string(query));

    std::vector<std::string> query_deletes;
    generate_deletes(std::string(query), gen_edits, query_deletes);
    for (const auto& del : query_deletes) {
      consider_word(del);
      collect_deletes(del);
    }

    collect_deletes(std::string(query));

    for (const std::uint32_t wid : candidates) {
      const std::string& word = words_by_id_[wid];
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
    multi_postings_.clear();
    words_by_id_.clear();
    word_to_id_.clear();
    deletes_ready_ = true;
  }

  // Build delete index now (no-op if already current). Safe to call after bulk load.
  void prepare() override { ensure_deletes_built(); }

  std::unique_ptr<FuzzyBackend> clone() const override {
    auto out = std::make_unique<SymSpellFuzzyBackend>(max_dict_edits_);
    out->dictionary_ = dictionary_;
    std::lock_guard lock(rebuild_mu_);
    if (deletes_ready_) {
      out->deletes_ = deletes_;
      out->multi_postings_ = multi_postings_;
      out->words_by_id_ = words_by_id_;
      out->word_to_id_ = word_to_id_;
      out->deletes_ready_ = true;
    } else {
      out->deletes_ready_ = false;
    }
    return out;
  }

  int max_dictionary_edit_distance() const { return max_dict_edits_; }

 private:
  static constexpr std::uint32_t kMultiFlag = 0x80000000u;
  static constexpr std::uint32_t kMultiMask = 0x7fffffffu;

  void mark_deletes_dirty() {
    std::lock_guard lock(rebuild_mu_);
    deletes_ready_ = false;
    deletes_.clear();
    multi_postings_.clear();
    words_by_id_.clear();
    word_to_id_.clear();
  }

  void ensure_deletes_built() const {
    std::lock_guard lock(rebuild_mu_);
    if (deletes_ready_) {
      return;
    }
    deletes_.clear();
    multi_postings_.clear();
    words_by_id_.clear();
    word_to_id_.clear();
    words_by_id_.reserve(dictionary_.size());
    word_to_id_.reserve(dictionary_.size());
    // ~O(len^2) unique deletes/word at d=2; probe @20k ≈ 120× words.
    deletes_.reserve(dictionary_.size() * 120);
    multi_postings_.reserve(dictionary_.size() * 16);
    for (const auto& [word, ids] : dictionary_) {
      if (ids.empty()) {
        continue;
      }
      const auto wid = static_cast<std::uint32_t>(words_by_id_.size());
      words_by_id_.push_back(word);
      word_to_id_.emplace(word, wid);
      index_deletes_for(word, wid);
    }
    deletes_ready_ = true;
  }

  void index_deletes_for(const std::string& word, std::uint32_t word_id) const {
    std::vector<std::string> dels;
    generate_deletes(word, max_dict_edits_, dels);
    for (auto& del : dels) {
      add_delete_posting(std::move(del), word_id);
    }
  }

  void add_delete_posting(std::string del, std::uint32_t word_id) const {
    auto [it, inserted] = deletes_.try_emplace(std::move(del), word_id);
    if (inserted) {
      return;  // first hit — store word id directly
    }
    std::uint32_t& ref = it->second;
    if ((ref & kMultiFlag) == 0) {
      const std::uint32_t first = ref;
      const auto idx = static_cast<std::uint32_t>(multi_postings_.size());
      multi_postings_.push_back({first, word_id});
      ref = idx | kMultiFlag;
      return;
    }
    multi_postings_[ref & kMultiMask].push_back(word_id);
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
  // Delete key → word id (MSB clear) or multi_postings_ index (MSB set).
  mutable std::unordered_map<std::string, std::uint32_t> deletes_;
  mutable std::vector<std::vector<std::uint32_t>> multi_postings_;
  mutable std::vector<std::string> words_by_id_;
  mutable std::unordered_map<std::string, std::uint32_t> word_to_id_;
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
