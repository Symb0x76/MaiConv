#pragma once

#include <filesystem>

namespace maiconv {

bool convert_audio_to_mp3(const std::filesystem::path& source,
                          const std::filesystem::path& target_mp3);

bool convert_acb_awb_to_mp3(const std::filesystem::path& acb,
                            const std::filesystem::path& awb,
                            const std::filesystem::path& target_mp3);

bool convert_mp3_to_acb_awb(const std::filesystem::path& source_mp3,
                            const std::filesystem::path& target_acb,
                            const std::filesystem::path& target_awb);

bool generate_silent_mp3(const std::filesystem::path& target_mp3,
                         double duration_seconds);

}  // namespace maiconv
