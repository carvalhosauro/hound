#include <iostream>
#include <string>

#include "hound/bulk_loader.hpp"
#include "hound/fuzzy_index.hpp"
#include "hound/snapshot.hpp"

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " <file.csv|file.json> [--snapshot PATH]\n";
    return 2;
  }
  const std::string path = argv[1];
  std::string snapshot;
  for (int i = 2; i < argc; ++i) {
    if (std::string(argv[i]) == "--snapshot" && i + 1 < argc) {
      snapshot = argv[++i];
    }
  }

  hound::FuzzyIndex index;
  const auto n = hound::load_bulk_file(index, path);
  std::cout << "loaded " << n << " documents\n";
  if (!snapshot.empty()) {
    hound::save_snapshot(index, snapshot);
    std::cout << "wrote snapshot " << snapshot << "\n";
  }
  return 0;
}
