#pragma once

#include <cctype>
#include <string>
#include <string_view>

namespace hound {

// MVP normalizer: keep printable ASCII, lowercase. Non-ASCII bytes are dropped.
inline std::string normalize(std::string_view input) {
  std::string out;
  out.reserve(input.size());
  for (unsigned char ch : input) {
    if (ch >= 128) {
      continue;
    }
    if (std::isspace(ch)) {
      if (!out.empty() && out.back() != ' ') {
        out.push_back(' ');
      }
      continue;
    }
    if (std::isalpha(ch) || std::isdigit(ch) || ch == '-' || ch == '_' || ch == '\'') {
      out.push_back(static_cast<char>(std::tolower(ch)));
    }
  }
  while (!out.empty() && out.back() == ' ') {
    out.pop_back();
  }
  return out;
}

}  // namespace hound
