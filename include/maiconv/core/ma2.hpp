#pragma once

#include "maiconv/core/chart.hpp"
#include "maiconv/core/format.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace maiconv {

class Ma2Tokenizer {
 public:
  std::vector<std::string> tokenize_file(const std::filesystem::path& path) const;
  std::vector<std::string> tokenize_text(const std::string& text) const;
};

class Ma2Parser {
 public:
  Chart parse(const std::vector<std::string>& lines) const;
};

class Ma2Composer {
 public:
  std::string compose(const Chart& chart, ChartFormat format) const;
};

}  // namespace maiconv