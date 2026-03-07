#pragma once

#include "maiconv/core/chart.hpp"
#include "maiconv/core/format.hpp"

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace maiconv {

struct SimaiDocument {
  std::unordered_map<std::string, std::string> metadata;
  std::map<int, std::vector<std::string>> chart_tokens;
};

class SimaiTokenizer {
 public:
  std::vector<std::string> tokenize_text(const std::string& text) const;
  std::vector<std::string> tokenize_file(const std::filesystem::path& path) const;
  SimaiDocument parse_document(const std::string& text) const;
  SimaiDocument parse_file(const std::filesystem::path& path) const;
};

class SimaiParser {
 public:
  Chart parse_tokens(const std::vector<std::string>& tokens) const;
  Chart parse_document(const SimaiDocument& document, int difficulty) const;

  static bool contains_slide_notation(const std::string& token);
  static std::vector<std::string> each_group_of_token(const std::string& token);
};

class SimaiComposer {
 public:
  std::string compose_chart(const Chart& chart) const;
};

}  // namespace maiconv