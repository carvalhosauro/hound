#include <benchmark/benchmark.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "hound/fuzzy_index.hpp"
#include "hound/score_merger.hpp"
#include "synth_generator/synth.hpp"

namespace {

constexpr std::uint64_t kSeed = 42;
constexpr std::uint64_t kTypoSeed = 99;

std::unique_ptr<hound::FuzzyIndex> build_index(std::size_t n) {
  hound::synth::GeneratorConfig cfg;
  cfg.count = n;
  cfg.seed = kSeed;
  auto docs = hound::synth::generate_documents(cfg);
  auto index = std::make_unique<hound::FuzzyIndex>();
  for (const auto& d : docs) {
    index->upsert(d);
  }
  return index;
}

std::vector<hound::Document> docs_for(std::size_t n) {
  hound::synth::GeneratorConfig cfg;
  cfg.count = n;
  cfg.seed = kSeed;
  return hound::synth::generate_documents(cfg);
}

std::string typo_query(const hound::Document& doc, int distance) {
  std::mt19937_64 rng(kTypoSeed);
  return hound::synth::apply_typos(doc.text, distance, rng);
}

void BM_Insert(benchmark::State& state) {
  const auto n = static_cast<std::size_t>(state.range(0));
  const auto docs = docs_for(n);
  for (auto _ : state) {
    state.PauseTiming();
    hound::FuzzyIndex index;
    state.ResumeTiming();
    for (const auto& d : docs) {
      index.upsert(d);
    }
    benchmark::DoNotOptimize(index.size());
  }
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(n));
}

void BM_SearchExact(benchmark::State& state) {
  const auto n = static_cast<std::size_t>(state.range(0));
  auto docs = docs_for(n);
  auto index = build_index(n);
  const std::string q = docs[n / 2].text;
  for (auto _ : state) {
    auto hits = index->search(q, {.limit = 10, .max_edit_distance = 0});
    benchmark::DoNotOptimize(hits.data());
    benchmark::ClobberMemory();
  }
}

void BM_SearchFuzzy(benchmark::State& state) {
  const auto n = static_cast<std::size_t>(state.range(0));
  const int distance = static_cast<int>(state.range(1));
  auto docs = docs_for(n);
  auto index = build_index(n);
  const std::string q = typo_query(docs[n / 2], distance);
  for (auto _ : state) {
    auto hits = index->search(q, {.limit = 10, .max_edit_distance = distance});
    benchmark::DoNotOptimize(hits.data());
    benchmark::ClobberMemory();
  }
}

void BM_ScoreMerge(benchmark::State& state) {
  const auto n = static_cast<std::size_t>(state.range(0));
  std::vector<hound::SearchHit> base;
  base.reserve(n);
  for (std::size_t i = 0; i < n; ++i) {
    hound::SearchHit h;
    h.id = "id-" + std::to_string(i);
    h.text_relevance = static_cast<double>(i % 100) / 100.0;
    h.external_score = static_cast<double>(i);
    base.push_back(std::move(h));
  }
  hound::ScoreMerger merger{{0.7}};
  for (auto _ : state) {
    auto candidates = base;
    auto ranked = merger.merge(std::move(candidates));
    benchmark::DoNotOptimize(ranked.data());
    benchmark::ClobberMemory();
  }
}

}  // namespace

BENCHMARK(BM_Insert)->Arg(1000)->Arg(5000)->Arg(20000)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_SearchExact)->Arg(1000)->Arg(5000)->Arg(20000)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_SearchFuzzy)
    ->Args({1000, 1})
    ->Args({1000, 2})
    ->Args({1000, 3})
    ->Args({5000, 1})
    ->Args({5000, 2})
    ->Args({5000, 3})
    ->Args({20000, 1})
    ->Args({20000, 2})
    ->Args({20000, 3})
    ->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_ScoreMerge)->Arg(64)->Arg(256)->Arg(1024)->Unit(benchmark::kMicrosecond);

BENCHMARK_MAIN();
