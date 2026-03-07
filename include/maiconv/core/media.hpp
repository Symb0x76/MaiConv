#pragma once

#include <filesystem>

namespace maiconv {

bool convert_audio_to_mp3(const std::filesystem::path& source, const std::filesystem::path& target_mp3);

bool convert_acb_awb_to_mp3(const std::filesystem::path& acb,
                            const std::filesystem::path& awb,
                            const std::filesystem::path& target_mp3);

bool convert_ab_to_png(const std::filesystem::path& ab_file, const std::filesystem::path& png_file);
bool convert_dat_or_usm_to_mp4(const std::filesystem::path& source, const std::filesystem::path& target_mp4);

}  // namespace maiconv
