#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

#include "hound/bulk_loader.hpp"
#include "hound/fuzzy_index.hpp"
#include "hound/http_api.hpp"
#include "hound/ranker_factory.hpp"
#include "hound/snapshot.hpp"

namespace {

void usage(const char* argv0) {
  std::cerr
      << "Usage: " << argv0
      << " [--host HOST] [--port PORT] [--snapshot PATH] [--load FILE]\n"
      << "       [--fuzzy-backend bk|symspell] [--ranker linear|tie_break]\n"
      << "       [--publish-swap]\n"
      << "\n"
      << "Hound — fuzzy autocomplete sidecar (HTTP JSON).\n"
      << "  --host            bind address (default 127.0.0.1)\n"
      << "  --port            listen port (default 8080)\n"
      << "  --snapshot        optional binary snapshot path (load on boot, save on writes)\n"
      << "  --load            bulk load .csv or .json before serving\n"
      << "  --fuzzy-backend   fuzzy dictionary: symspell (default) or bk\n"
      << "                    (overrides HOUND_FUZZY_BACKEND env if set)\n"
      << "  --ranker          ranking: linear (default α blend) or tie_break\n"
      << "                    (overrides HOUND_RANKER env if set; per-query\n"
      << "                    ?ranker= still overrides for a single search)\n"
      << "  --publish-swap    E2: readers use atomic snapshot publish (slower writes);\n"
      << "                    default is shared_mutex. Also HOUND_PUBLISH_SWAP=1\n";
}

}  // namespace

int main(int argc, char** argv) {
  std::string host = "127.0.0.1";
  int port = 8080;
  std::string snapshot;
  std::string load_path;
  hound::FuzzyBackendKind fuzzy_kind = hound::fuzzy_backend_kind_from_env();
  hound::RankerKind ranker_kind = hound::ranker_kind_from_env();
  hound::PublishMode publish_mode = hound::publish_mode_from_env();

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    auto need = [&](const char* name) -> std::string {
      if (i + 1 >= argc) {
        std::cerr << "missing value for " << name << "\n";
        std::exit(2);
      }
      return argv[++i];
    };
    if (arg == "--host") {
      host = need("--host");
    } else if (arg == "--port") {
      port = std::stoi(need("--port"));
    } else if (arg == "--snapshot") {
      snapshot = need("--snapshot");
    } else if (arg == "--load") {
      load_path = need("--load");
    } else if (arg == "--fuzzy-backend") {
      const std::string value = need("--fuzzy-backend");
      if (!hound::parse_fuzzy_backend_kind(value, fuzzy_kind)) {
        std::cerr << "invalid --fuzzy-backend: " << value << " (use bk or symspell)\n";
        return 2;
      }
    } else if (arg == "--ranker") {
      const std::string value = need("--ranker");
      if (!hound::parse_ranker_kind(value, ranker_kind)) {
        std::cerr << "invalid --ranker: " << value << " (use linear or tie_break)\n";
        return 2;
      }
    } else if (arg == "--publish-swap") {
      publish_mode = hound::PublishMode::PublishSwap;
    } else if (arg == "--help" || arg == "-h") {
      usage(argv[0]);
      return 0;
    } else {
      std::cerr << "unknown arg: " << arg << "\n";
      usage(argv[0]);
      return 2;
    }
  }

  hound::FuzzyIndex index(hound::make_fuzzy_backend(fuzzy_kind),
                          hound::make_ranker(ranker_kind), publish_mode);
  std::cerr << "fuzzy backend: "
            << (fuzzy_kind == hound::FuzzyBackendKind::SymSpell ? "symspell" : "bk")
            << "\n";
  std::cerr << "ranker: " << hound::ranker_kind_name(ranker_kind) << "\n";
  std::cerr << "publish mode: "
            << (publish_mode == hound::PublishMode::PublishSwap ? "publish_swap" : "legacy")
            << "\n";
  if (!snapshot.empty()) {
    std::ifstream probe(snapshot, std::ios::binary);
    if (probe.good()) {
      probe.close();
      try {
        hound::load_snapshot(index, snapshot);
        index.prepare();
        std::cerr << "loaded snapshot: " << snapshot << " (" << index.size() << " docs)\n";
      } catch (const std::exception& ex) {
        std::cerr << "warning: could not load snapshot: " << ex.what() << "\n";
      }
    }
  }

  if (!load_path.empty()) {
    const auto n = hound::load_bulk_file(index, load_path);
    index.prepare();
    std::cerr << "bulk loaded " << n << " docs from " << load_path << "\n";
    if (!snapshot.empty()) {
      hound::save_snapshot(index, snapshot);
    }
  }

  hound::HttpApi api(index, snapshot);
  std::cerr << "hound listening on http://" << host << ":" << port << "\n";
  if (!api.listen(host, port)) {
    std::cerr << "failed to bind " << host << ":" << port << "\n";
    return 1;
  }
  return 0;
}
