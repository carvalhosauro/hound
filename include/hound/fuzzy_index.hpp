#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <thread>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "hound/adaptive_edit_distance.hpp"
#include "hound/document.hpp"
#include "hound/symspell_backend.hpp"  // FuzzyBackend + factory (SymSpell default)
#include "hound/normalizer.hpp"
#include "hound/ranker_factory.hpp"
#include "hound/trie.hpp"

namespace hound {

struct SearchOptions {
  std::size_t limit = 10;
  double alpha = 0.7;
  // nullopt → adaptive table (Phase C). Explicit int → fixed distance (override).
  std::optional<int> max_edit_distance;
  std::size_t prefix_candidate_limit = 64;
  // nullopt → index-owned Ranker. Explicit → per-query override (Phase D3).
  std::optional<RankerKind> ranker;
};

// Legacy = shared_mutex in-place (default). PublishSwap = copy→mutate→atomic publish (E2).
enum class PublishMode { Legacy, PublishSwap };

inline PublishMode publish_mode_from_env() {
  const char* raw = std::getenv("HOUND_PUBLISH_SWAP");
  if (raw == nullptr || raw[0] == '\0') {
    return PublishMode::Legacy;
  }
  const std::string_view v(raw);
  if (v == "1" || v == "true" || v == "TRUE" || v == "yes" || v == "on") {
    return PublishMode::PublishSwap;
  }
  return PublishMode::Legacy;
}

inline std::chrono::milliseconds consolidate_ms_from_env() {
  const char* raw = std::getenv("HOUND_CONSOLIDATE_MS");
  if (raw == nullptr || raw[0] == '\0') {
    return std::chrono::milliseconds{0};
  }
  char* end = nullptr;
  const unsigned long v = std::strtoul(raw, &end, 10);
  if (end == raw || (end != nullptr && *end != '\0')) {
    return std::chrono::milliseconds{0};
  }
  return std::chrono::milliseconds{v};
}

struct IndexState {
  std::unordered_map<std::string, Document> docs;
  Trie trie;
  std::unique_ptr<FuzzyBackend> fuzzy;

  IndexState() : fuzzy(make_default_fuzzy_backend()) {}

  explicit IndexState(std::unique_ptr<FuzzyBackend> f) : fuzzy(std::move(f)) {
    if (!fuzzy) {
      fuzzy = make_default_fuzzy_backend();
    }
  }

  IndexState(const IndexState& other)
      : docs(other.docs), trie(other.trie), fuzzy(other.fuzzy ? other.fuzzy->clone() : nullptr) {
    if (!fuzzy) {
      fuzzy = make_default_fuzzy_backend();
    }
  }

  IndexState& operator=(const IndexState& other) {
    if (this != &other) {
      docs = other.docs;
      trie = other.trie;
      fuzzy = other.fuzzy ? other.fuzzy->clone() : make_default_fuzzy_backend();
    }
    return *this;
  }

  IndexState(IndexState&&) noexcept = default;
  IndexState& operator=(IndexState&&) noexcept = default;
};

// Thread-safe in-memory index: shared locks for search, unique for mutations (legacy),
// or publish-swap readers via atomic shared_ptr (E2 opt-in).
// Fuzzy dictionary is pluggable via FuzzyBackend (default: SymSpell after B4).
// Ranking is pluggable via Ranker (default: ScoreMerger linear blend).
class FuzzyIndex {
 public:
  explicit FuzzyIndex(std::unique_ptr<FuzzyBackend> fuzzy = make_default_fuzzy_backend(),
                      std::unique_ptr<Ranker> ranker = make_default_ranker(),
                      PublishMode mode = PublishMode::Legacy,
                      std::chrono::milliseconds consolidate_ms = std::chrono::milliseconds{0})
      : mode_(mode),
        consolidate_ms_(mode == PublishMode::PublishSwap ? consolidate_ms
                                                         : std::chrono::milliseconds{0}),
        ranker_(std::move(ranker)),
        state_(std::move(fuzzy)) {
    if (!ranker_) {
      ranker_ = make_default_ranker();
    }
    if (mode_ == PublishMode::PublishSwap) {
      draft_ = IndexState(state_.fuzzy ? state_.fuzzy->clone() : make_default_fuzzy_backend());
      // Start with empty published matching draft (also empty).
      published_.store(std::make_shared<const IndexState>(draft_), std::memory_order_release);
    }
    start_consolidator_if_needed();
  }

  ~FuzzyIndex() { stop_consolidator(); }

  // Defer snapshot publish across many upserts (bulk load). Call prepare() after
  // to rebuild fuzzy structures and publish once. No-op in Legacy mode.
  void begin_bulk() {
    if (mode_ == PublishMode::PublishSwap) {
      std::lock_guard wlock(writer_mu_);
      defer_publish_ = true;
    }
  }

  void upsert(Document doc) {
    const std::string normalized = normalize(doc.text);
    if (mode_ == PublishMode::PublishSwap) {
      std::lock_guard wlock(writer_mu_);
      apply_upsert(draft_, std::move(doc), normalized);
      if (!defer_publish_) {
        if (deferred_publish_active()) {
          dirty_ = true;
        } else {
          publish_draft_unlocked();
        }
      } else if (deferred_publish_active()) {
        dirty_ = true;
      }
      return;
    }
    std::unique_lock lock(mu_);
    apply_upsert(state_, std::move(doc), normalized);
  }

  bool erase(const std::string& id) {
    if (mode_ == PublishMode::PublishSwap) {
      std::lock_guard wlock(writer_mu_);
      if (!apply_erase(draft_, id)) {
        return false;
      }
      if (!defer_publish_) {
        if (deferred_publish_active()) {
          dirty_ = true;
        } else {
          publish_draft_unlocked();
        }
      } else if (deferred_publish_active()) {
        dirty_ = true;
      }
      return true;
    }
    std::unique_lock lock(mu_);
    return apply_erase(state_, id);
  }

  std::optional<Document> get(const std::string& id) const {
    if (mode_ == PublishMode::PublishSwap) {
      auto snap = published_.load(std::memory_order_acquire);
      auto it = snap->docs.find(id);
      if (it == snap->docs.end()) {
        return std::nullopt;
      }
      return it->second;
    }
    std::shared_lock lock(mu_);
    auto it = state_.docs.find(id);
    if (it == state_.docs.end()) {
      return std::nullopt;
    }
    return it->second;
  }

  std::size_t size() const {
    if (mode_ == PublishMode::PublishSwap) {
      return published_.load(std::memory_order_acquire)->docs.size();
    }
    std::shared_lock lock(mu_);
    return state_.docs.size();
  }

  void clear() {
    if (mode_ == PublishMode::PublishSwap) {
      std::lock_guard wlock(writer_mu_);
      draft_.docs.clear();
      draft_.trie.clear();
      draft_.fuzzy->clear();
      publish_draft_unlocked();
      return;
    }
    std::unique_lock lock(mu_);
    state_.docs.clear();
    state_.trie.clear();
    state_.fuzzy->clear();
  }

  // Finish deferred fuzzy-index work (e.g. SymSpell delete map) after bulk load.
  void prepare() {
    if (mode_ == PublishMode::PublishSwap) {
      std::lock_guard wlock(writer_mu_);
      publish_draft_unlocked();
      defer_publish_ = false;
      return;
    }
    std::unique_lock lock(mu_);
    state_.fuzzy->prepare();
  }

  // Copy under shared lock for snapshot serialization.
  std::vector<Document> copy_documents() const {
    if (mode_ == PublishMode::PublishSwap) {
      auto snap = published_.load(std::memory_order_acquire);
      std::vector<Document> out;
      out.reserve(snap->docs.size());
      for (const auto& [_, doc] : snap->docs) {
        out.push_back(doc);
      }
      return out;
    }
    std::shared_lock lock(mu_);
    std::vector<Document> out;
    out.reserve(state_.docs.size());
    for (const auto& [_, doc] : state_.docs) {
      out.push_back(doc);
    }
    return out;
  }

  std::vector<SearchHit> search(std::string_view query, SearchOptions opt = {}) const {
    const std::string q = normalize(query);
    const int max_edits = resolve_max_edit_distance(q.size(), opt.max_edit_distance);
    std::unordered_map<std::string, SearchHit> by_id;

    auto run_search = [&](const IndexState& st) {
      auto consider = [&](const std::string& id, int distance, bool prefix_bonus) {
        auto dit = st.docs.find(id);
        if (dit == st.docs.end()) {
          return;
        }
        double rel = distance_to_relevance(distance, max_edits);
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

      if (q.empty()) {
        return;
      }
      auto comps = st.trie.completions(q, opt.prefix_candidate_limit);
      for (const auto& [key, ids] : comps) {
        const int dist =
            static_cast<int>(key.size() >= q.size() ? key.size() - q.size() : 0);
        const int edit = (key == q) ? 0 : std::min(dist, max_edits);
        for (const auto& id : ids) {
          consider(id, edit, true);
        }
      }

      auto fuzzy = st.fuzzy->search(q, max_edits);
      for (const auto& m : fuzzy) {
        for (const auto& id : m.ids) {
          consider(id, m.distance, false);
        }
      }
    };

    if (mode_ == PublishMode::PublishSwap) {
      auto snap = published_.load(std::memory_order_acquire);
      run_search(*snap);
    } else {
      std::shared_lock lock(mu_);
      run_search(state_);
    }

    std::vector<SearchHit> candidates;
    candidates.reserve(by_id.size());
    for (auto& [_, hit] : by_id) {
      candidates.push_back(std::move(hit));
    }

    std::vector<SearchHit> ranked;
    if (opt.ranker.has_value()) {
      // Per-query override (HTTP ?ranker=); ephemeral ranker is empty/cheap.
      auto override_ranker = make_ranker(*opt.ranker);
      ranked = override_ranker->rank(std::move(candidates), RankOptions{opt.alpha});
    } else {
      ranked = ranker_->rank(std::move(candidates), RankOptions{opt.alpha});
    }
    if (ranked.size() > opt.limit) {
      ranked.resize(opt.limit);
    }
    return ranked;
  }

  PublishMode publish_mode() const { return mode_; }

  std::chrono::milliseconds consolidate_ms() const { return consolidate_ms_; }

 private:
  void publish_draft_unlocked() {
    draft_.fuzzy->prepare();
    published_.store(std::make_shared<const IndexState>(draft_), std::memory_order_release);
    dirty_ = false;
  }

  bool deferred_publish_active() const {
    return mode_ == PublishMode::PublishSwap && consolidate_ms_.count() > 0;
  }

  void start_consolidator_if_needed() {
    if (!deferred_publish_active()) {
      return;
    }
    worker_ = std::jthread([this](std::stop_token st) {
      std::stop_callback on_stop(st, [this] { cv_.notify_all(); });
      std::unique_lock lock(writer_mu_);
      while (!st.stop_requested()) {
        cv_.wait_for(lock, consolidate_ms_, [&] { return st.stop_requested(); });
        if (st.stop_requested()) {
          break;
        }
        if (dirty_ && !defer_publish_) {
          publish_draft_unlocked();
        }
      }
    });
  }

  void stop_consolidator() {
    if (worker_.joinable()) {
      worker_.request_stop();
      cv_.notify_all();
      worker_ = std::jthread{};
    }
  }

  static void apply_upsert(IndexState& st, Document doc, const std::string& normalized) {
    auto it = st.docs.find(doc.id);
    if (it != st.docs.end()) {
      const std::string old_norm = normalize(it->second.text);
      st.trie.erase(old_norm, doc.id);
      st.fuzzy->erase(old_norm, doc.id);
    }
    st.docs[doc.id] = doc;
    if (!normalized.empty()) {
      st.trie.insert(normalized, doc.id);
      st.fuzzy->insert(normalized, doc.id);
    }
  }

  static bool apply_erase(IndexState& st, const std::string& id) {
    auto it = st.docs.find(id);
    if (it == st.docs.end()) {
      return false;
    }
    const std::string norm = normalize(it->second.text);
    st.trie.erase(norm, id);
    st.fuzzy->erase(norm, id);
    st.docs.erase(it);
    return true;
  }

  PublishMode mode_;
  std::chrono::milliseconds consolidate_ms_{0};
  bool dirty_ = false;
  std::unique_ptr<Ranker> ranker_;

  // Legacy path
  mutable std::shared_mutex mu_;
  IndexState state_;

  // PublishSwap path: mutate draft_ under writer_mu_, publish copies for readers.
  mutable std::mutex writer_mu_;
  IndexState draft_;
  std::atomic<std::shared_ptr<const IndexState>> published_;
  bool defer_publish_ = false;
  std::condition_variable cv_;
  std::jthread worker_;
};

}  // namespace hound
