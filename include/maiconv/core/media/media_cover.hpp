#pragma once

#include <filesystem>

namespace maiconv {

bool convert_ab_to_png(const std::filesystem::path& ab_file,
                       const std::filesystem::path& png_file);

}  // namespace maiconv
