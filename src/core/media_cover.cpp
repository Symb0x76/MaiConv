#include "maiconv/core/media/media_cover.hpp"

#include "maiconv/core/media/media_shared.hpp"

#include <mutex>

namespace maiconv
{
    bool extract_unity_texture_bundle_to_png(const std::filesystem::path &ab_file,
                                             const std::filesystem::path &png_file);
}

namespace maiconv
{

    bool convert_ab_to_png(const std::filesystem::path &ab_file,
                           const std::filesystem::path &png_file)
    {
        if (!media_shared_file_non_empty(ab_file))
        {
            return false;
        }
        if (media_shared_write_embedded_png(ab_file, png_file))
        {
            return true;
        }

        static std::mutex unity_bundle_decode_mutex;
        constexpr int kDecodeAttempts = 3;
        for (int attempt = 0; attempt < kDecodeAttempts; ++attempt)
        {
            std::lock_guard<std::mutex> guard(unity_bundle_decode_mutex);
            if (extract_unity_texture_bundle_to_png(ab_file, png_file))
            {
                return true;
            }
        }
        return false;
    }

} // namespace maiconv
