#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <thread>

#include "hound/fuzzy_index.hpp"
#include "hound/http_api.hpp"

TEST_CASE("HTTP search integration", "[integration][http]") {
  hound::FuzzyIndex index;
  index.upsert({"1", "Finn Field", 8.0});
  index.upsert({"2", "Gray Grove", 2.0});

  hound::HttpApi api(index);
  // Bind ephemeral port.
  const int port = api.server().bind_to_any_port("127.0.0.1");
  REQUIRE(port > 0);

  std::thread th([&] { api.server().listen_after_bind(); });

  // Give the server a moment.
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  httplib::Client client("127.0.0.1", port);
  client.set_connection_timeout(1, 0);
  client.set_read_timeout(1, 0);

  auto health = client.Get("/health");
  REQUIRE(health);
  REQUIRE(health->status == 200);

  auto indexed = client.Post("/index", R"({"id":"3","text":"Harper Hill","external_score":5})",
                             "application/json");
  REQUIRE(indexed);
  REQUIRE(indexed->status == 200);

  auto search = client.Get("/search?q=finn%20field&limit=5");
  REQUIRE(search);
  REQUIRE(search->status == 200);
  REQUIRE(search->body.find("\"id\":\"1\"") != std::string::npos);

  auto typo = client.Get("/search?q=fin%20field&limit=5");
  REQUIRE(typo);
  REQUIRE(typo->status == 200);

  auto del = client.Delete("/index/3");
  REQUIRE(del);
  REQUIRE(del->status == 200);

  api.stop();
  th.join();
}
