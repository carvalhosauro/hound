#pragma once

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace hound {

inline int levenshtein(std::string_view a, std::string_view b) {
  const std::size_t n = a.size();
  const std::size_t m = b.size();
  if (n == 0) {
    return static_cast<int>(m);
  }
  if (m == 0) {
    return static_cast<int>(n);
  }
  std::vector<int> prev(m + 1);
  std::vector<int> cur(m + 1);
  for (std::size_t j = 0; j <= m; ++j) {
    prev[j] = static_cast<int>(j);
  }
  for (std::size_t i = 1; i <= n; ++i) {
    cur[0] = static_cast<int>(i);
    for (std::size_t j = 1; j <= m; ++j) {
      const int cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
      cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost});
    }
    prev.swap(cur);
  }
  return prev[m];
}

// Burkhard-Keller tree over normalized strings. Each unique key stores one or
// more document ids. Deletes are lazy (tombstones) and skipped on search.
class BkTree {
 public:
  void insert(std::string key, std::string id) {
    if (!root_) {
      root_ = std::make_unique<Node>(std::move(key));
      root_->ids.push_back(std::move(id));
      return;
    }
    Node* node = root_.get();
    while (true) {
      if (node->key == key) {
        if (std::find(node->ids.begin(), node->ids.end(), id) == node->ids.end()) {
          node->ids.push_back(std::move(id));
        }
        node->deleted = false;
        return;
      }
      const int dist = levenshtein(node->key, key);
      auto it = node->children.find(dist);
      if (it == node->children.end()) {
        node->children.emplace(dist, std::make_unique<Node>(std::move(key)));
        node->children[dist]->ids.push_back(std::move(id));
        return;
      }
      node = it->second.get();
    }
  }

  void erase(std::string_view key, const std::string& id) {
    Node* node = find_node(key);
    if (!node) {
      return;
    }
    node->ids.erase(std::remove(node->ids.begin(), node->ids.end(), id), node->ids.end());
    if (node->ids.empty()) {
      node->deleted = true;
    }
  }

  struct Match {
    std::string key;
    std::vector<std::string> ids;
    int distance = 0;
  };

  std::vector<Match> search(std::string_view query, int max_distance) const {
    std::vector<Match> out;
    if (!root_) {
      return out;
    }
    search_rec(root_.get(), query, max_distance, out);
    return out;
  }

  void clear() { root_.reset(); }

  bool empty() const { return root_ == nullptr; }

 private:
  struct Node {
    explicit Node(std::string k) : key(std::move(k)) {}
    std::string key;
    std::vector<std::string> ids;
    bool deleted = false;
    std::unordered_map<int, std::unique_ptr<Node>> children;
  };

  Node* find_node(std::string_view key) const {
    if (!root_) {
      return nullptr;
    }
    Node* node = root_.get();
    while (node) {
      if (node->key == key) {
        return node;
      }
      const int dist = levenshtein(node->key, key);
      auto it = node->children.find(dist);
      if (it == node->children.end()) {
        return nullptr;
      }
      node = it->second.get();
    }
    return nullptr;
  }

  static void search_rec(const Node* node, std::string_view query, int max_distance,
                         std::vector<Match>& out) {
    if (!node) {
      return;
    }
    const int dist = levenshtein(node->key, query);
    if (!node->deleted && dist <= max_distance && !node->ids.empty()) {
      out.push_back(Match{node->key, node->ids, dist});
    }
    for (const auto& [edge, child] : node->children) {
      if (edge >= dist - max_distance && edge <= dist + max_distance) {
        search_rec(child.get(), query, max_distance, out);
      }
    }
  }

  std::unique_ptr<Node> root_;
};

}  // namespace hound
