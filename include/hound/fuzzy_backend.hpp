#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "hound/bk_tree.hpp"

namespace hound {

// Fuzzy match returned by FuzzyBackend::search (ids sharing one normalized key).
struct FuzzyMatch {
  std::string key;
  std::vector<std::string> ids;
  int distance = 0;
};

// Pluggable fuzzy dictionary (Phase B). Default implementation is selected via
// FuzzyBackendKind (SymSpell after B4 when metrics win; override with BK).
class FuzzyBackend {
 public:
  virtual ~FuzzyBackend() = default;

  virtual void insert(std::string key, std::string id) = 0;
  virtual void erase(std::string_view key, const std::string& id) = 0;
  virtual std::vector<FuzzyMatch> search(std::string_view query, int max_distance) const = 0;
  virtual void clear() = 0;
  // Optional: finish deferred index work after bulk ingest (SymSpell delete map).
  virtual void prepare() {}
};

// BK-tree adapter — default fuzzy path; behavior must stay aligned with BkTree.
class BkFuzzyBackend final : public FuzzyBackend {
 public:
  void insert(std::string key, std::string id) override {
    tree_.insert(std::move(key), std::move(id));
  }

  void erase(std::string_view key, const std::string& id) override { tree_.erase(key, id); }

  std::vector<FuzzyMatch> search(std::string_view query, int max_distance) const override {
    auto raw = tree_.search(query, max_distance);
    std::vector<FuzzyMatch> out;
    out.reserve(raw.size());
    for (auto& m : raw) {
      out.push_back(FuzzyMatch{std::move(m.key), std::move(m.ids), m.distance});
    }
    return out;
  }

  void clear() override { tree_.clear(); }

 private:
  BkTree tree_;
};

// Runtime / compile selection for FuzzyBackend.
// B4: SymSpell is the default when metrics win; force BK with
// -DHOUND_DEFAULT_FUZZY_BACKEND_BK or HOUND_FUZZY_BACKEND=bk / --fuzzy-backend bk.
enum class FuzzyBackendKind { BkTree, SymSpell };

#if defined(HOUND_DEFAULT_FUZZY_BACKEND_BK)
inline constexpr FuzzyBackendKind kCompileDefaultFuzzyBackend = FuzzyBackendKind::BkTree;
#else
inline constexpr FuzzyBackendKind kCompileDefaultFuzzyBackend = FuzzyBackendKind::SymSpell;
#endif

inline FuzzyBackendKind default_fuzzy_backend_kind() { return kCompileDefaultFuzzyBackend; }

}  // namespace hound
