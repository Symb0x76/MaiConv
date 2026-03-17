#pragma once

#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace maiconv
{

    struct SimaiDocument
    {
        std::unordered_map<std::string, std::string> metadata;
        std::map<int, std::vector<std::string>> chart_tokens;
    };

} // namespace maiconv
