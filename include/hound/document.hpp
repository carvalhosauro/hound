#pragma once

#include <string>

namespace hound {

struct Document {
  std::string id;
  std::string text;
  double external_score = 0.0;
};

struct SearchHit {
  std::string id;
  double score = 0.0;
  double text_relevance = 0.0;
  double external_score = 0.0;
};

}  // namespace hound
