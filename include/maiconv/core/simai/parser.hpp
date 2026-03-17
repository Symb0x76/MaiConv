#pragma once

#include "maiconv/core/simai/chart.hpp"
#include "maiconv/core/simai/document.hpp"

#include <string>
#include <vector>

namespace maiconv::simai
{

    class Parser
    {
    public:
        Chart parse_tokens(const std::vector<std::string> &tokens) const;
        Chart parse_document(const SimaiDocument &document, int difficulty) const;

        static bool contains_slide_notation(const std::string &token);
        static std::vector<std::string> each_group_of_token(const std::string &token);
    };

} // namespace maiconv::simai