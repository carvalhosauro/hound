#pragma once

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace hound {

// Prefix trie mapping normalized text -> set of document ids.
class Trie {
 public:
  Trie() : root_(std::make_unique<Node>()) {}

  void insert(std::string_view key, const std::string& id) {
    Node* node = root_.get();
    for (char ch : key) {
      auto& child = node->children[ch];
      if (!child) {
        child = std::make_unique<Node>();
      }
      node = child.get();
    }
    node->ids.insert(id);
    node->is_terminal = true;
  }

  void erase(std::string_view key, const std::string& id) {
    erase_rec(root_.get(), key, 0, id);
  }

  bool contains(std::string_view key) const {
    const Node* node = walk(key);
    return node != nullptr && node->is_terminal && !node->ids.empty();
  }

  std::vector<std::pair<std::string, std::unordered_set<std::string>>>
  completions(std::string_view prefix, std::size_t limit) const {
    std::vector<std::pair<std::string, std::unordered_set<std::string>>> out;
    const Node* node = walk(prefix);
    if (!node) {
      return out;
    }
    std::string path(prefix);
    collect(node, path, limit, out);
    return out;
  }

  void clear() {
    root_ = std::make_unique<Node>();
  }

 private:
  struct Node {
    std::unordered_map<char, std::unique_ptr<Node>> children;
    std::unordered_set<std::string> ids;
    bool is_terminal = false;
  };

  const Node* walk(std::string_view key) const {
    const Node* node = root_.get();
    for (char ch : key) {
      auto it = node->children.find(ch);
      if (it == node->children.end()) {
        return nullptr;
      }
      node = it->second.get();
    }
    return node;
  }

  static bool erase_rec(Node* node, std::string_view key, std::size_t depth,
                        const std::string& id) {
    if (!node) {
      return false;
    }
    if (depth == key.size()) {
      node->ids.erase(id);
      if (node->ids.empty()) {
        node->is_terminal = false;
      }
      return node->children.empty() && node->ids.empty();
    }
    char ch = key[depth];
    auto it = node->children.find(ch);
    if (it == node->children.end()) {
      return false;
    }
    if (erase_rec(it->second.get(), key, depth + 1, id)) {
      node->children.erase(it);
    }
    return node->children.empty() && node->ids.empty() && !node->is_terminal;
  }

  static void collect(const Node* node, std::string& path, std::size_t limit,
                      std::vector<std::pair<std::string, std::unordered_set<std::string>>>& out) {
    if (out.size() >= limit) {
      return;
    }
    if (node->is_terminal && !node->ids.empty()) {
      out.emplace_back(path, node->ids);
      if (out.size() >= limit) {
        return;
      }
    }
    std::vector<char> keys;
    keys.reserve(node->children.size());
    for (const auto& [ch, _] : node->children) {
      keys.push_back(ch);
    }
    std::sort(keys.begin(), keys.end());
    for (char ch : keys) {
      path.push_back(ch);
      collect(node->children.at(ch).get(), path, limit, out);
      path.pop_back();
      if (out.size() >= limit) {
        return;
      }
    }
  }

  std::unique_ptr<Node> root_;
};

}  // namespace hound
