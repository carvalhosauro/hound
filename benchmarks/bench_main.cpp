#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <random>
#include <string>
#include <vector>

#include "hound/fuzzy_index.hpp"
#include "synth_generator/synth.hpp"

namespace {

struct Percentiles {
  double p50 = 0;
  double p95 = 0;
  double p99 = 0;
};

Percentiles percentiles_us(std::vector<double> samples) {
  if (samples.empty()) {
    return {};
  }
  std::sort(samples.begin(), samples.end());
  auto at = [&](double p) {
    const double idx = p * static_cast<double>(samples.size() - 1);
    const std::size_t i = static_cast<std::size_t>(idx);
    return samples[i];
  };
  return {at(0.50), at(0.95), at(0.99)};
}

double rss_mb() {
  std::ifstream status("/proc/self/status");
  std::string key;
  while (status >> key) {
    if (key == "VmRSS:") {
      long kb = 0;
      status >> kb;
      return static_cast<double>(kb) / 1024.0;
    }
    status.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
  }
  return 0.0;
}

void run_size(std::size_t n) {
  hound::synth::GeneratorConfig cfg;
  cfg.count = n;
  cfg.seed = 42;

  auto docs = hound::synth::generate_documents(cfg);
  hound::FuzzyIndex index;

  const auto t0 = std::chrono::steady_clock::now();
  for (const auto& d : docs) {
    index.upsert(d);
  }
  const auto t1 = std::chrono::steady_clock::now();
  const double ingest_ms =
      std::chrono::duration<double, std::milli>(t1 - t0).count();

  std::mt19937_64 rng(99);
  std::uniform_int_distribution<std::size_t> pick(0, docs.size() - 1);
  std::vector<std::string> queries;
  std::vector<std::string> targets;
  queries.reserve(200);
  targets.reserve(200);
  for (std::size_t i = 0; i < 200; ++i) {
    const std::size_t di = pick(rng);
    targets.push_back(docs[di].id);
    queries.push_back(hound::synth::apply_typos(docs[di].text, 1, rng));
  }

  std::vector<double> lat_us;
  lat_us.reserve(queries.size());
  std::size_t hits_at_10 = 0;

  for (std::size_t i = 0; i < queries.size(); ++i) {
    const auto s0 = std::chrono::steady_clock::now();
    auto results = index.search(queries[i], {.limit = 10, .max_edit_distance = 2});
    const auto s1 = std::chrono::steady_clock::now();
    lat_us.push_back(std::chrono::duration<double, std::micro>(s1 - s0).count());
    for (const auto& h : results) {
      if (h.id == targets[i]) {
        ++hits_at_10;
        break;
      }
    }
  }

  const auto pct = percentiles_us(std::move(lat_us));
  const double recall = static_cast<double>(hits_at_10) / static_cast<double>(queries.size());
  const double mem = rss_mb();

  std::cout << "size=" << n << " ingest_ms=" << ingest_ms << " p50_us=" << pct.p50
            << " p95_us=" << pct.p95 << " p99_us=" << pct.p99 << " recall@10=" << recall
            << " rss_mb=" << mem << "\n";
}

}  // namespace

int main() {
  std::cout << "hound_bench (synthetic)\n";
  for (std::size_t n : {std::size_t{1000}, std::size_t{5000}, std::size_t{20000}}) {
    run_size(n);
  }
  return 0;
}
