#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

#include "hound/bulk_loader.hpp"
#include "hound/fuzzy_index.hpp"
#include "hound/http_api.hpp"
#include "hound/snapshot.hpp"

namespace {

void usage(const char* argv0) {
  std::cerr
      << "Usage: " << argv0
      << " [--host HOST] [--port PORT] [--snapshot PATH] [--load FILE]\n"
      << "\n"
      << "Hound — fuzzy autocomplete sidecar (HTTP JSON).\n"
      << "  --host       bind address (default 127.0.0.1)\n"
      << "  --port       listen port (default 8080)\n"
      << "  --snapshot   optional binary snapshot path (load on boot, save on writes)\n"
      << "  --load       bulk load .csv or .json before serving\n";
}

}  // namespace

int main(int argc, char** argv) {
  std::string host = "127.0.0.1";
  int port = 8080;
  std::string snapshot;
  std::string load_path;

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
    } else if (arg == "--help" || arg == "-h") {
      usage(argv[0]);
      return 0;
    } else {
      std::cerr << "unknown arg: " << arg << "\n";
      usage(argv[0]);
      return 2;
    }
  }

  hound::FuzzyIndex index;
  if (!snapshot.empty()) {
    std::ifstream probe(snapshot, std::ios::binary);
    if (probe.good()) {
      probe.close();
      try {
        hound::load_snapshot(index, snapshot);
        std::cerr << "loaded snapshot: " << snapshot << " (" << index.size() << " docs)\n";
      } catch (const std::exception& ex) {
        std::cerr << "warning: could not load snapshot: " << ex.what() << "\n";
      }
    }
  }

  if (!load_path.empty()) {
    const auto n = hound::load_bulk_file(index, load_path);
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
