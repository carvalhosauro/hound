#pragma once

#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "hound/document.hpp"

namespace hound::synth {

struct GeneratorConfig {
  std::uint64_t seed = 42;
  std::size_t count = 1000;
  int max_typo_distance = 2;
};

inline const std::vector<std::string>& first_names() {
  static const std::vector<std::string> v = {
      "Ada",    "Blake", "Casey", "Drew",  "Eden",  "Finn",  "Gray",  "Harper",
      "Indigo", "Jules", "Kai",   "Lane",  "Morgan", "Noel", "Oakley", "Parker",
      "Quinn",  "Reese", "Sage",  "Taylor", "Uma",  "Vale",  "Wren",  "Xander",
      "Yael",   "Zane"};
  return v;
}

inline const std::vector<std::string>& last_names() {
  static const std::vector<std::string> v = {
      "Ash",   "Brook", "Cedar", "Dale",  "Elm",   "Field", "Grove", "Hill",
      "Isle",  "Jade",  "Knot",  "Lake",  "Meadow", "North", "Orchid", "Pine",
      "Quill", "Ridge", "Stone", "Thorn", "Underwood", "Vale", "Wood", "York"};
  return v;
}

inline std::vector<Document> generate_documents(const GeneratorConfig& cfg) {
  std::mt19937_64 rng(cfg.seed);
  std::uniform_int_distribution<std::size_t> fi(0, first_names().size() - 1);
  std::uniform_int_distribution<std::size_t> li(0, last_names().size() - 1);
  std::uniform_real_distribution<double> score(0.0, 100.0);

  std::vector<Document> docs;
  docs.reserve(cfg.count);
  for (std::size_t i = 0; i < cfg.count; ++i) {
    Document d;
    d.id = "doc-" + std::to_string(i);
    // Unique synthetic labels: name pair + base36-ish suffix keeps typos local.
    d.text = first_names()[fi(rng)] + " " + last_names()[li(rng)] + " " +
             std::to_string(i);
    d.external_score = score(rng);
    docs.push_back(std::move(d));
  }
  return docs;
}

// Apply up to `distance` random edit operations (insert/delete/substitute).
inline std::string apply_typos(std::string text, int distance, std::mt19937_64& rng) {
  if (text.empty() || distance <= 0) {
    return text;
  }
  std::uniform_int_distribution<int> op_dist(0, 2);
  std::uniform_int_distribution<int> letter('a', 'z');
  for (int n = 0; n < distance; ++n) {
    if (text.empty()) {
      text.push_back(static_cast<char>(letter(rng)));
      continue;
    }
    std::uniform_int_distribution<std::size_t> pos(0, text.size() - 1);
    const int op = op_dist(rng);
    const std::size_t i = pos(rng);
    if (op == 0) {  // substitute
      text[i] = static_cast<char>(letter(rng));
    } else if (op == 1) {  // delete
      text.erase(i, 1);
    } else {  // insert
      text.insert(i, 1, static_cast<char>(letter(rng)));
    }
  }
  return text;
}

inline std::vector<std::string> generate_typo_queries(const std::vector<Document>& docs,
                                                      std::size_t count, int distance,
                                                      std::uint64_t seed) {
  std::mt19937_64 rng(seed);
  std::uniform_int_distribution<std::size_t> pick(0, docs.size() - 1);
  std::vector<std::string> queries;
  queries.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    queries.push_back(apply_typos(docs[pick(rng)].text, distance, rng));
  }
  return queries;
}

}  // namespace hound::synth
