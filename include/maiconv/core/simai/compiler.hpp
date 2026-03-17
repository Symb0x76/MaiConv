#pragma once

#include "maiconv/core/simai/chart.hpp"

#include <string>

namespace maiconv::simai
{

    class Compiler
    {
    public:
        std::string compile_chart(const Chart &chart) const;
    };

} // namespace maiconv::simai