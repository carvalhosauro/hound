#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <thread>

#include <nlohmann/json.hpp>

#include "hound/fuzzy_index.hpp"
#include "hound/http_api.hpp"
#include "hound/ranker_factory.hpp"

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
  {
    auto hj = nlohmann::json::parse(health->body);
    REQUIRE(hj.at("status") == "ok");
    REQUIRE(hj.contains("version"));
    REQUIRE(hj.at("version").get<std::string>().size() > 0);
    REQUIRE(hj.at("size").get<std::size_t>() == 2);
    REQUIRE(hj.at("publish_mode") == "legacy");
    REQUIRE(hj.at("consolidate_ms").get<long long>() == 0);
  }

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

TEST_CASE("HTTP ranker query param (D3)", "[integration][http][ranker][d3]") {
  hound::FuzzyIndex index;
  // Same text → equal text_relevance; external_score decides under tie_break.
  index.upsert({"cold", "Same Text", 1.0});
  index.upsert({"hot", "Same Text", 100.0});

  hound::HttpApi api(index);
  const int port = api.server().bind_to_any_port("127.0.0.1");
  REQUIRE(port > 0);
  std::thread th([&] { api.server().listen_after_bind(); });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  httplib::Client client("127.0.0.1", port);
  client.set_connection_timeout(1, 0);
  client.set_read_timeout(1, 0);

  auto parse_ids = [](const std::string& body) {
    auto j = nlohmann::json::parse(body);
    std::vector<std::string> ids;
    for (const auto& r : j.at("results")) {
      ids.push_back(r.at("id").get<std::string>());
    }
    return ids;
  };

  // Default / linear + alpha=1 → equal scores → id asc: cold before hot.
  auto linear = client.Get("/search?q=same%20text&limit=5&alpha=1&ranker=linear");
  REQUIRE(linear);
  REQUIRE(linear->status == 200);
  auto linear_ids = parse_ids(linear->body);
  REQUIRE(linear_ids.size() == 2);
  REQUIRE(linear_ids.front() == "cold");
  REQUIRE(linear_ids.back() == "hot");
  // Default JSON shape unchanged (no new fields).
  auto linear_json = nlohmann::json::parse(linear->body);
  REQUIRE(linear_json["results"][0].contains("score"));
  REQUIRE(linear_json["results"][0].contains("text_relevance"));
  REQUIRE(linear_json["results"][0].contains("external_score"));
  REQUIRE_FALSE(linear_json["results"][0].contains("ranker"));

  // tie_break → external desc: hot before cold.
  auto tie = client.Get("/search?q=same%20text&limit=5&ranker=tie_break");
  REQUIRE(tie);
  REQUIRE(tie->status == 200);
  auto tie_ids = parse_ids(tie->body);
  REQUIRE(tie_ids.size() == 2);
  REQUIRE(tie_ids.front() == "hot");
  REQUIRE(tie_ids.back() == "cold");

  // Omit ranker → same shape as linear path (process default = ScoreMerger).
  auto omitted = client.Get("/search?q=same%20text&limit=5&alpha=1");
  REQUIRE(omitted);
  REQUIRE(omitted->status == 200);
  REQUIRE(parse_ids(omitted->body) == linear_ids);

  auto bad = client.Get("/search?q=same%20text&ranker=nope");
  REQUIRE(bad);
  REQUIRE(bad->status == 400);

  api.stop();
  th.join();
}

TEST_CASE("parse_ranker_kind accepts linear and tie_break", "[ranker][d3]") {
  hound::RankerKind kind = hound::RankerKind::TieBreak;
  REQUIRE(hound::parse_ranker_kind("linear", kind));
  REQUIRE(kind == hound::RankerKind::Linear);
  REQUIRE(hound::parse_ranker_kind("score_merger", kind));
  REQUIRE(kind == hound::RankerKind::Linear);
  REQUIRE(hound::parse_ranker_kind("tie_break", kind));
  REQUIRE(kind == hound::RankerKind::TieBreak);
  REQUIRE(hound::parse_ranker_kind("tiebreak", kind));
  REQUIRE(kind == hound::RankerKind::TieBreak);
  REQUIRE_FALSE(hound::parse_ranker_kind("nope", kind));
  REQUIRE(std::string(hound::ranker_kind_name(hound::RankerKind::Linear)) == "linear");
  REQUIRE(std::string(hound::ranker_kind_name(hound::RankerKind::TieBreak)) == "tie_break");
}

TEST_CASE("HTTP attrs filter equality", "[integration][http][attrs][h1]") {
  hound::FuzzyIndex index;
  hound::HttpApi api(index);
  const int port = api.server().bind_to_any_port("127.0.0.1");
  REQUIRE(port > 0);
  std::thread th([&] { api.server().listen_after_bind(); });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  httplib::Client client("127.0.0.1", port);
  client.set_connection_timeout(1, 0);
  client.set_read_timeout(1, 0);

  auto a = client.Post(
      "/index",
      R"({"id":"1","text":"Queijo Mussarela","external_score":1,"attrs":{"tenant":"a"}})",
      "application/json");
  REQUIRE(a);
  REQUIRE(a->status == 200);
  auto b = client.Post(
      "/index",
      R"({"id":"2","text":"Queijo Mussarela","external_score":9,"attrs":{"tenant":"b"}})",
      "application/json");
  REQUIRE(b);
  REQUIRE(b->status == 200);

  auto filtered = client.Get("/search?q=queijo&limit=10&attrs.tenant=a");
  REQUIRE(filtered);
  REQUIRE(filtered->status == 200);
  auto body = nlohmann::json::parse(filtered->body);
  REQUIRE(body["results"].size() == 1);
  REQUIRE(body["results"][0]["id"] == "1");
  REQUIRE_FALSE(body["results"][0].contains("attrs"));

  auto unfiltered = client.Get("/search?q=queijo&limit=10");
  REQUIRE(unfiltered);
  REQUIRE(unfiltered->status == 200);
  REQUIRE(nlohmann::json::parse(unfiltered->body)["results"].size() == 2);

  api.stop();
  th.join();
}

TEST_CASE("HTTP attrs upsert wipe and empty value", "[integration][http][attrs][h1]") {
  hound::FuzzyIndex index;
  hound::HttpApi api(index);
  const int port = api.server().bind_to_any_port("127.0.0.1");
  REQUIRE(port > 0);
  std::thread th([&] { api.server().listen_after_bind(); });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  httplib::Client client("127.0.0.1", port);
  client.set_connection_timeout(1, 0);
  client.set_read_timeout(1, 0);

  REQUIRE(client.Post(
              "/index",
              R"({"id":"1","text":"Item","external_score":1,"attrs":{"tenant":"a","open":""}})",
              "application/json")
              ->status == 200);

  auto empty_open = client.Get("/search?q=item&limit=10&attrs.open=");
  REQUIRE(empty_open);
  REQUIRE(empty_open->status == 200);
  REQUIRE(nlohmann::json::parse(empty_open->body)["results"].size() == 1);

  REQUIRE(client.Post("/index", R"({"id":"1","text":"Item","external_score":1})",
                      "application/json")
              ->status == 200);
  auto after_wipe = client.Get("/search?q=item&limit=10&attrs.tenant=a");
  REQUIRE(after_wipe);
  REQUIRE(after_wipe->status == 200);
  REQUIRE(nlohmann::json::parse(after_wipe->body)["results"].empty());

  REQUIRE(client.Post(
              "/index",
              R"({"id":"2","text":"Item","external_score":1,"attrs":{"tenant":"b"}})",
              "application/json")
              ->status == 200);
  REQUIRE(client.Post("/index",
                      R"({"id":"2","text":"Item","external_score":1,"attrs":{}})",
                      "application/json")
              ->status == 200);
  auto wiped_obj = client.Get("/search?q=item&limit=10&attrs.tenant=b");
  REQUIRE(wiped_obj);
  REQUIRE(wiped_obj->status == 200);
  REQUIRE(nlohmann::json::parse(wiped_obj->body)["results"].empty());

  api.stop();
  th.join();
}

TEST_CASE("HTTP attrs validation 400s and last-wins", "[integration][http][attrs][h1]") {
  hound::FuzzyIndex index;
  hound::HttpApi api(index);
  const int port = api.server().bind_to_any_port("127.0.0.1");
  REQUIRE(port > 0);
  std::thread th([&] { api.server().listen_after_bind(); });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  httplib::Client client("127.0.0.1", port);
  client.set_connection_timeout(1, 0);
  client.set_read_timeout(1, 0);

  auto expect400 = [&](auto resp) {
    REQUIRE(resp);
    REQUIRE(resp->status == 400);
    auto j = nlohmann::json::parse(resp->body);
    REQUIRE(j.contains("error"));
    REQUIRE(j["error"].is_string());
  };

  expect400(client.Post(
      "/index", R"({"id":"3","text":"X","external_score":1,"attrs":{"tenant":1}})",
      "application/json"));
  expect400(client.Post(
      "/index", R"({"id":"3","text":"X","external_score":1,"attrs":[]})",
      "application/json"));
  expect400(client.Post(
      "/index", R"({"id":"3","text":"X","external_score":1,"attrs":"x"})",
      "application/json"));
  expect400(client.Post(
      "/index", R"({"id":"3","text":"X","external_score":1,"attrs":null})",
      "application/json"));
  expect400(client.Post(
      "/index", R"({"id":"3","text":"X","external_score":1,"attrs":{"":"v"}})",
      "application/json"));

  expect400(client.Get("/search?q=x&attrs.=v"));

  REQUIRE(client.Post(
              "/index",
              R"({"id":"1","text":"Queijo","external_score":1,"attrs":{"tenant":"a"}})",
              "application/json")
              ->status == 200);
  REQUIRE(client.Post(
              "/index",
              R"({"id":"2","text":"Queijo","external_score":9,"attrs":{"tenant":"b"}})",
              "application/json")
              ->status == 200);

  auto last = client.Get("/search?q=queijo&limit=10&attrs.tenant=a&attrs.tenant=b");
  REQUIRE(last);
  REQUIRE(last->status == 200);
  auto results = nlohmann::json::parse(last->body)["results"];
  REQUIRE(results.size() == 1);
  REQUIRE(results[0]["id"] == "2");

  api.stop();
  th.join();
}

TEST_CASE("HTTP attrs on bulk index", "[integration][http][attrs][h1]") {
  hound::FuzzyIndex index;
  hound::HttpApi api(index);
  const int port = api.server().bind_to_any_port("127.0.0.1");
  REQUIRE(port > 0);
  std::thread th([&] { api.server().listen_after_bind(); });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  httplib::Client client("127.0.0.1", port);
  client.set_connection_timeout(1, 0);
  client.set_read_timeout(1, 0);

  auto bulk = client.Post(
      "/index/bulk",
      R"([
        {"id":"1","text":"Alpha","external_score":1,"attrs":{"tenant":"a"}},
        {"id":"2","text":"Alpha","external_score":2,"attrs":{"tenant":"b"}}
      ])",
      "application/json");
  REQUIRE(bulk);
  REQUIRE(bulk->status == 200);

  auto filtered = client.Get("/search?q=alpha&limit=10&attrs.tenant=a");
  REQUIRE(filtered);
  REQUIRE(filtered->status == 200);
  auto body = nlohmann::json::parse(filtered->body);
  REQUIRE(body["results"].size() == 1);
  REQUIRE(body["results"][0]["id"] == "1");
  REQUIRE_FALSE(body["results"][0].contains("attrs"));

  api.stop();
  th.join();
}
