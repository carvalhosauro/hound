#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "hound/bk_tree.hpp"
#include "hound/fuzzy_backend.hpp"

namespace hound {

// BK-tree FuzzyBackend — **oracle / fallback only** (Phase B5).
// Not on the default hot path (SymSpell is). Kept for:
//   - parity tests vs SymSpell
//   - escape hatch: --fuzzy-backend bk / HOUND_FUZZY_BACKEND=bk
//   - -DHOUND_DEFAULT_FUZZY_BACKEND_BK
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

}  // namespace hound
