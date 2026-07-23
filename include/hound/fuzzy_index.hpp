#pragma once

#include <algorithm>
#include <cstddef>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "hound/bk_tree.hpp"
#include "hound/document.hpp"
#include "hound/normalizer.hpp"
#include "hound/score_merger.hpp"
#include "hound/trie.hpp"

namespace hound {

struct SearchOptions {
  std::size_t limit = 10;
  double alpha = 0.7;
  int max_edit_distance = 2;
  std::size_t prefix_candidate_limit = 64;
};

// Thread-safe in-memory index: shared locks for search, unique for mutations.
class FuzzyIndex {
 public:
  void upsert(Document doc) {
    const std::string normalized = normalize(doc.text);
    std::unique_lock lock(mu_);
    auto it = docs_.find(doc.id);
    if (it != docs_.end()) {
      const std::string old_norm = normalize(it->second.text);
      trie_.erase(old_norm, doc.id);
      bk_.erase(old_norm, doc.id);
    }
    docs_[doc.id] = doc;
    if (!normalized.empty()) {
      trie_.insert(normalized, doc.id);
      bk_.insert(normalized, doc.id);
    }
  }

  bool erase(const std::string& id) {
    std::unique_lock lock(mu_);
    auto it = docs_.find(id);
    if (it == docs_.end()) {
      return false;
    }
    const std::string norm = normalize(it->second.text);
    trie_.erase(norm, id);
    bk_.erase(norm, id);
    docs_.erase(it);
    return true;
  }

  std::optional<Document> get(const std::string& id) const {
    std::shared_lock lock(mu_);
    auto it = docs_.find(id);
    if (it == docs_.end()) {
      return std::nullopt;
    }
    return it->second;
  }

  std::size_t size() const {
    std::shared_lock lock(mu_);
    return docs_.size();
  }

  void clear() {
    std::unique_lock lock(mu_);
    docs_.clear();
    trie_.clear();
    bk_.clear();
  }

  // Copy under shared lock for snapshot serialization.
  std::vector<Document> copy_documents() const {
    std::shared_lock lock(mu_);
    std::vector<Document> out;
    out.reserve(docs_.size());
    for (const auto& [_, doc] : docs_) {
      out.push_back(doc);
    }
    return out;
  }

  std::vector<SearchHit> search(std::string_view query, SearchOptions opt = {}) const {
    const std::string q = normalize(query);
    std::unordered_map<std::string, SearchHit> by_id;

    auto consider = [&](const std::string& id, int distance, bool prefix_bonus) {
      auto dit = docs_.find(id);
      if (dit == docs_.end()) {
        return;
      }
      double rel = distance_to_relevance(distance, opt.max_edit_distance);
      if (prefix_bonus) {
        rel = std::min(1.0, rel + 0.15);
      }
      auto hit_it = by_id.find(id);
      if (hit_it == by_id.end()) {
        SearchHit hit;
        hit.id = id;
        hit.text_relevance = rel;
        hit.external_score = dit->second.external_score;
        by_id.emplace(id, std::move(hit));
      } else {
        hit_it->second.text_relevance = std::max(hit_it->second.text_relevance, rel);
      }
    };

    {
      std::shared_lock lock(mu_);
      if (!q.empty()) {
        auto comps = trie_.completions(q, opt.prefix_candidate_limit);
        for (const auto& [key, ids] : comps) {
          const int dist =
              static_cast<int>(key.size() >= q.size() ? key.size() - q.size() : 0);
          const int edit = (key == q) ? 0 : std::min(dist, opt.max_edit_distance);
          for (const auto& id : ids) {
            consider(id, edit, true);
          }
        }

        auto fuzzy = bk_.search(q, opt.max_edit_distance);
        for (const auto& m : fuzzy) {
          for (const auto& id : m.ids) {
            consider(id, m.distance, false);
          }
        }
      }
    }

    std::vector<SearchHit> candidates;
    candidates.reserve(by_id.size());
    for (auto& [_, hit] : by_id) {
      candidates.push_back(std::move(hit));
    }

    ScoreMerger merger{{opt.alpha}};
    auto ranked = merger.merge(std::move(candidates));
    if (ranked.size() > opt.limit) {
      ranked.resize(opt.limit);
    }
    return ranked;
  }

 private:
  mutable std::shared_mutex mu_;
  std::unordered_map<std::string, Document> docs_;
  Trie trie_;
  BkTree bk_;
};

}  // namespace hound
