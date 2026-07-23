#pragma once

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "hound/document.hpp"
#include "hound/fuzzy_index.hpp"

namespace hound {

namespace detail {

inline std::string trim(std::string_view s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
    s.remove_prefix(1);
  }
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
    s.remove_suffix(1);
  }
  return std::string(s);
}

inline std::vector<std::string> split_csv_line(const std::string& line) {
  std::vector<std::string> fields;
  std::string cur;
  bool in_quotes = false;
  for (std::size_t i = 0; i < line.size(); ++i) {
    char ch = line[i];
    if (in_quotes) {
      if (ch == '"') {
        if (i + 1 < line.size() && line[i + 1] == '"') {
          cur.push_back('"');
          ++i;
        } else {
          in_quotes = false;
        }
      } else {
        cur.push_back(ch);
      }
    } else {
      if (ch == '"') {
        in_quotes = true;
      } else if (ch == ',') {
        fields.push_back(cur);
        cur.clear();
      } else {
        cur.push_back(ch);
      }
    }
  }
  fields.push_back(cur);
  return fields;
}

}  // namespace detail

// Load generic CSV with header: id,text,external_score
inline std::size_t load_csv(FuzzyIndex& index, const std::string& path) {
  std::ifstream in(path);
  if (!in) {
    throw std::runtime_error("bulk: cannot open CSV: " + path);
  }
  std::string line;
  if (!std::getline(in, line)) {
    return 0;
  }
  auto header = detail::split_csv_line(line);
  int id_i = -1, text_i = -1, score_i = -1;
  for (std::size_t i = 0; i < header.size(); ++i) {
    const auto h = detail::trim(header[i]);
    if (h == "id") {
      id_i = static_cast<int>(i);
    } else if (h == "text") {
      text_i = static_cast<int>(i);
    } else if (h == "external_score") {
      score_i = static_cast<int>(i);
    }
  }
  if (id_i < 0 || text_i < 0 || score_i < 0) {
    throw std::runtime_error("bulk: CSV header must include id,text,external_score");
  }

  std::size_t count = 0;
  while (std::getline(in, line)) {
    if (detail::trim(line).empty()) {
      continue;
    }
    auto fields = detail::split_csv_line(line);
    const auto max_i = static_cast<std::size_t>(std::max({id_i, text_i, score_i}));
    if (fields.size() <= max_i) {
      throw std::runtime_error("bulk: CSV row too short");
    }
    Document doc;
    doc.id = detail::trim(fields[static_cast<std::size_t>(id_i)]);
    doc.text = fields[static_cast<std::size_t>(text_i)];
    doc.external_score = std::stod(detail::trim(fields[static_cast<std::size_t>(score_i)]));
    index.upsert(std::move(doc));
    ++count;
  }
  return count;
}

// Minimal JSON array loader: [{"id":"...","text":"...","external_score":1.0}, ...]
// Intentionally small — full JSON for HTTP uses nlohmann/json.
inline std::size_t load_json_array(FuzzyIndex& index, const std::string& path) {
  std::ifstream in(path);
  if (!in) {
    throw std::runtime_error("bulk: cannot open JSON: " + path);
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  const std::string content = ss.str();

  auto skip_ws = [&](std::size_t& i) {
    while (i < content.size() && std::isspace(static_cast<unsigned char>(content[i]))) {
      ++i;
    }
  };
  auto parse_string = [&](std::size_t& i) -> std::string {
    if (i >= content.size() || content[i] != '"') {
      throw std::runtime_error("bulk: expected string");
    }
    ++i;
    std::string out;
    while (i < content.size()) {
      char ch = content[i++];
      if (ch == '"') {
        return out;
      }
      if (ch == '\\' && i < content.size()) {
        char esc = content[i++];
        if (esc == '"' || esc == '\\' || esc == '/') {
          out.push_back(esc);
        } else if (esc == 'n') {
          out.push_back('\n');
        } else if (esc == 't') {
          out.push_back('\t');
        } else {
          out.push_back(esc);
        }
      } else {
        out.push_back(ch);
      }
    }
    throw std::runtime_error("bulk: unterminated string");
  };

  std::size_t i = 0;
  skip_ws(i);
  if (i >= content.size() || content[i] != '[') {
    throw std::runtime_error("bulk: JSON root must be an array");
  }
  ++i;

  std::size_t count = 0;
  while (true) {
    skip_ws(i);
    if (i < content.size() && content[i] == ']') {
      break;
    }
    if (i >= content.size() || content[i] != '{') {
      throw std::runtime_error("bulk: expected object");
    }
    ++i;
    Document doc;
    bool have_id = false, have_text = false, have_score = false;
    while (true) {
      skip_ws(i);
      if (i < content.size() && content[i] == '}') {
        ++i;
        break;
      }
      auto key = parse_string(i);
      skip_ws(i);
      if (i >= content.size() || content[i] != ':') {
        throw std::runtime_error("bulk: expected ':'");
      }
      ++i;
      skip_ws(i);
      if (key == "id" || key == "text") {
        auto val = parse_string(i);
        if (key == "id") {
          doc.id = std::move(val);
          have_id = true;
        } else {
          doc.text = std::move(val);
          have_text = true;
        }
      } else if (key == "external_score") {
        std::size_t start = i;
        while (i < content.size() &&
               (std::isdigit(static_cast<unsigned char>(content[i])) || content[i] == '.' ||
                content[i] == '-' || content[i] == '+' || content[i] == 'e' ||
                content[i] == 'E')) {
          ++i;
        }
        doc.external_score = std::stod(content.substr(start, i - start));
        have_score = true;
      } else {
        // skip unknown value (string or number)
        if (i < content.size() && content[i] == '"') {
          (void)parse_string(i);
        } else {
          while (i < content.size() && content[i] != ',' && content[i] != '}') {
            ++i;
          }
        }
      }
      skip_ws(i);
      if (i < content.size() && content[i] == ',') {
        ++i;
        continue;
      }
    }
    if (!have_id || !have_text || !have_score) {
      throw std::runtime_error("bulk: object missing id/text/external_score");
    }
    index.upsert(std::move(doc));
    ++count;
    skip_ws(i);
    if (i < content.size() && content[i] == ',') {
      ++i;
      continue;
    }
  }
  return count;
}

inline std::size_t load_bulk_file(FuzzyIndex& index, const std::string& path) {
  if (path.size() >= 4 && path.substr(path.size() - 4) == ".csv") {
    return load_csv(index, path);
  }
  if (path.size() >= 5 && path.substr(path.size() - 5) == ".json") {
    return load_json_array(index, path);
  }
  throw std::runtime_error("bulk: unsupported extension (use .csv or .json)");
}

}  // namespace hound
