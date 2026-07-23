#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace hound {

// Fuzzy match returned by FuzzyBackend::search (ids sharing one normalized key).
struct FuzzyMatch {
  std::string key;
  std::vector<std::string> ids;
  int distance = 0;
};

// Pluggable fuzzy dictionary. Production default is SymSpell (see symspell_backend.hpp).
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

// Runtime / compile selection. Default = SymSpell (B4/B5).
// Force BK oracle/fallback with -DHOUND_DEFAULT_FUZZY_BACKEND_BK,
// HOUND_FUZZY_BACKEND=bk, or --fuzzy-backend bk.
enum class FuzzyBackendKind {
  BkTree,  // oracle / escape hatch — not the hot path
  SymSpell
};

#if defined(HOUND_DEFAULT_FUZZY_BACKEND_BK)
inline constexpr FuzzyBackendKind kCompileDefaultFuzzyBackend = FuzzyBackendKind::BkTree;
#else
inline constexpr FuzzyBackendKind kCompileDefaultFuzzyBackend = FuzzyBackendKind::SymSpell;
#endif

inline FuzzyBackendKind default_fuzzy_backend_kind() { return kCompileDefaultFuzzyBackend; }

}  // namespace hound
