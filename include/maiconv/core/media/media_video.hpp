#pragma once

#include <filesystem>

namespace maiconv {

bool convert_dat_or_usm_to_mp4(const std::filesystem::path& source,
                               const std::filesystem::path& target_mp4);

bool generate_single_frame_mp4_from_image(
    const std::filesystem::path& source_image,
    const std::filesystem::path& target_mp4);

bool generate_single_frame_black_mp4(const std::filesystem::path& target_mp4);

bool convert_mp4_to_dat(const std::filesystem::path& source_mp4,
                        const std::filesystem::path& target_dat);

}  // namespace maiconv
