#pragma once

#include <algorithm>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include "hound/bulk_loader.hpp"
#include "hound/fuzzy_index.hpp"
#include "hound/snapshot.hpp"
#include "hound/version.hpp"

namespace hound {

namespace {
inline std::map<std::string, std::string> parse_attrs_object(const nlohmann::json& body) {
  std::map<std::string, std::string> attrs;
  if (!body.contains("attrs")) {
    return attrs;
  }
  const auto& a = body.at("attrs");
  if (!a.is_object()) {
    throw std::runtime_error("attrs must be an object");
  }
  for (auto it = a.begin(); it != a.end(); ++it) {
    if (it.key().empty()) {
      throw std::runtime_error("attrs key must be non-empty");
    }
    if (!it.value().is_string()) {
      throw std::runtime_error("attrs values must be strings");
    }
    attrs.emplace(it.key(), it.value().get<std::string>());
  }
  return attrs;
}
}  // namespace

class HttpApi {
 public:
  explicit HttpApi(FuzzyIndex& index, std::string snapshot_path = {})
      : index_(index), snapshot_path_(std::move(snapshot_path)) {
    // Concurrent request handling (default is also a thread pool; set explicitly).
    server_.new_task_queue = [] {
      return new httplib::ThreadPool(std::max(4u, std::thread::hardware_concurrency()));
    };
    setup_routes();
  }

  httplib::Server& server() { return server_; }

  bool listen(const std::string& host, int port) { return server_.listen(host, port); }

  void stop() { server_.stop(); }

 private:
  void setup_routes() {
    server_.Get("/health", [this](const httplib::Request&, httplib::Response& res) {
      const char* mode =
          index_.publish_mode() == PublishMode::PublishSwap ? "publish_swap" : "legacy";
      nlohmann::json out{{"status", "ok"},
                         {"version", version_string()},
                         {"size", index_.size()},
                         {"publish_mode", mode},
                         {"consolidate_ms", index_.consolidate_ms().count()}};
      res.set_content(out.dump(), "application/json");
    });

    server_.Post("/index", [this](const httplib::Request& req, httplib::Response& res) {
      try {
        auto body = nlohmann::json::parse(req.body);
        Document doc;
        doc.id = body.at("id").get<std::string>();
        doc.text = body.at("text").get<std::string>();
        doc.external_score = body.value("external_score", 0.0);
        doc.attrs = parse_attrs_object(body);
        {
          std::lock_guard lock(mu_);
          index_.upsert(std::move(doc));
          maybe_save();
        }
        res.status = 200;
        res.set_content(R"({"ok":true})", "application/json");
      } catch (const std::exception& ex) {
        res.status = 400;
        nlohmann::json err{{"error", ex.what()}};
        res.set_content(err.dump(), "application/json");
      }
    });

    server_.Post("/index/bulk", [this](const httplib::Request& req, httplib::Response& res) {
      try {
        auto body = nlohmann::json::parse(req.body);
        if (!body.is_array()) {
          throw std::runtime_error("body must be a JSON array");
        }
        std::size_t count = 0;
        {
          std::lock_guard lock(mu_);
          for (const auto& item : body) {
            Document doc;
            doc.id = item.at("id").get<std::string>();
            doc.text = item.at("text").get<std::string>();
            doc.external_score = item.value("external_score", 0.0);
            doc.attrs = parse_attrs_object(item);
            index_.upsert(std::move(doc));
            ++count;
          }
          maybe_save();
        }
        nlohmann::json out{{"ok", true}, {"count", count}};
        res.set_content(out.dump(), "application/json");
      } catch (const std::exception& ex) {
        res.status = 400;
        nlohmann::json err{{"error", ex.what()}};
        res.set_content(err.dump(), "application/json");
      }
    });

    server_.Delete(R"(/index/(.+))", [this](const httplib::Request& req, httplib::Response& res) {
      const std::string id = req.matches[1];
      bool removed = false;
      {
        std::lock_guard lock(mu_);
        removed = index_.erase(id);
        if (removed) {
          maybe_save();
        }
      }
      if (!removed) {
        res.status = 404;
        res.set_content(R"({"error":"not found"})", "application/json");
        return;
      }
      res.set_content(R"({"ok":true})", "application/json");
    });

    server_.Get("/search", [this](const httplib::Request& req, httplib::Response& res) {
      try {
        if (!req.has_param("q")) {
          throw std::runtime_error("missing q");
        }
        SearchOptions opt;
        opt.limit = 10;
        if (req.has_param("limit")) {
          opt.limit = static_cast<std::size_t>(std::stoul(req.get_param_value("limit")));
        }
        if (req.has_param("alpha")) {
          opt.alpha = std::stod(req.get_param_value("alpha"));
        }
        // Optional fixed override; omit for adaptive length→distance (Phase C).
        if (req.has_param("max_edit_distance")) {
          opt.max_edit_distance = std::stoi(req.get_param_value("max_edit_distance"));
        }
        // Optional ranker override; omit for process default (ScoreMerger / --ranker).
        if (req.has_param("ranker")) {
          RankerKind kind = RankerKind::Linear;
          if (!parse_ranker_kind(req.get_param_value("ranker"), kind)) {
            throw std::runtime_error("invalid ranker (use linear or tie_break)");
          }
          opt.ranker = kind;
        }
        constexpr std::string_view kPrefix = "attrs.";
        for (const auto& [key, value] : req.params) {
          if (key.size() >= kPrefix.size() &&
              std::string_view(key).substr(0, kPrefix.size()) == kPrefix) {
            const std::string attr_key = key.substr(kPrefix.size());
            if (attr_key.empty()) {
              throw std::runtime_error("attrs key must be non-empty");
            }
            opt.attr_filters[attr_key] = value;
          }
        }
        const std::string q = req.get_param_value("q");
        // FuzzyIndex is internally synchronized; do not hold API write lock on reads.
        auto hits = index_.search(q, opt);
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& h : hits) {
          arr.push_back({{"id", h.id},
                         {"score", h.score},
                         {"text_relevance", h.text_relevance},
                         {"external_score", h.external_score}});
        }
        nlohmann::json out{{"results", arr}};
        res.set_content(out.dump(), "application/json");
      } catch (const std::exception& ex) {
        res.status = 400;
        nlohmann::json err{{"error", ex.what()}};
        res.set_content(err.dump(), "application/json");
      }
    });
  }

  void maybe_save() {
    if (!snapshot_path_.empty()) {
      save_snapshot(index_, snapshot_path_);
    }
  }

  FuzzyIndex& index_;
  std::string snapshot_path_;
  httplib::Server server_;
  std::mutex mu_;
};

}  // namespace hound
