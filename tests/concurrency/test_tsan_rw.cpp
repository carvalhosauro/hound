#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "hound/fuzzy_index.hpp"

TEST_CASE("concurrent search with upsert under TSan", "[tsan][concurrency]") {
  hound::FuzzyIndex index;
  for (int i = 0; i < 50; ++i) {
    index.upsert({"seed-" + std::to_string(i), "Seed Name " + std::to_string(i), static_cast<double>(i)});
  }

  std::atomic<bool> stop{false};
  std::atomic<std::uint64_t> searches{0};
  std::atomic<std::uint64_t> upserts{0};

  auto searcher = [&] {
    while (!stop.load(std::memory_order_relaxed)) {
      auto hits = index.search("seed name", {.limit = 10, .max_edit_distance = 2});
      (void)hits;
      searches.fetch_add(1, std::memory_order_relaxed);
    }
  };

  auto writer = [&] {
    int i = 1000;
    while (!stop.load(std::memory_order_relaxed)) {
      const std::string id = "w-" + std::to_string(i);
      index.upsert({id, "Writer Name " + std::to_string(i), 1.0});
      if (i % 3 == 0) {
        index.erase(id);
      }
      ++i;
      upserts.fetch_add(1, std::memory_order_relaxed);
    }
  };

  std::vector<std::thread> threads;
  threads.emplace_back(searcher);
  threads.emplace_back(searcher);
  threads.emplace_back(writer);

  std::this_thread::sleep_for(std::chrono::milliseconds(800));
  stop.store(true, std::memory_order_relaxed);
  for (auto& t : threads) {
    t.join();
  }

  REQUIRE(searches.load() > 0);
  REQUIRE(upserts.load() > 0);
  REQUIRE(index.size() >= 50);
}
