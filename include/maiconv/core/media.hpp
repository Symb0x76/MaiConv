#pragma once

#include <filesystem>

namespace maiconv {

bool convert_audio_to_mp3(const std::filesystem::path &source,
                          const std::filesystem::path &target_mp3);

bool convert_acb_awb_to_mp3(const std::filesystem::path &acb,
                            const std::filesystem::path &awb,
                            const std::filesystem::path &target_mp3);

bool convert_mp3_to_acb_awb(const std::filesystem::path &source_mp3,
                            const std::filesystem::path &target_acb,
                            const std::filesystem::path &target_awb);

bool convert_ab_to_png(const std::filesystem::path &ab_file,
                       const std::filesystem::path &png_file);
bool convert_dat_or_usm_to_mp4(const std::filesystem::path &source,
                               const std::filesystem::path &target_mp4);
bool generate_silent_mp3(const std::filesystem::path &target_mp3,
                         double duration_seconds);
bool generate_single_frame_mp4_from_image(
    const std::filesystem::path &source_image,
    const std::filesystem::path &target_mp4);
bool generate_single_frame_black_mp4(const std::filesystem::path &target_mp4);
bool convert_mp4_to_dat(const std::filesystem::path &source_mp4,
                        const std::filesystem::path &target_dat);

} // namespace maiconv
