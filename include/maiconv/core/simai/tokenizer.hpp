#pragma once

#include "maiconv/core/simai/document.hpp"

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace maiconv::simai
{

    class Tokenizer
    {
    public:
        std::vector<std::string> tokenize_text(const std::string &text) const;
        std::vector<std::string> tokenize_file(const std::filesystem::path &path) const;
        SimaiDocument parse_document(const std::string &text) const;
        SimaiDocument parse_file(const std::filesystem::path &path) const;
    };

} // namespace maiconv::simai