#include "maiconv/core/media/media_cover.hpp"

#include "maiconv/core/media/media_shared.hpp"

namespace maiconv {
bool extract_unity_texture_bundle_to_png(const std::filesystem::path &ab_file,
                                         const std::filesystem::path &png_file);
}

namespace maiconv {

bool convert_ab_to_png(const std::filesystem::path &ab_file,
                       const std::filesystem::path &png_file) {
  if (!media_shared_file_non_empty(ab_file)) {
    return false;
  }
  if (media_shared_write_embedded_png(ab_file, png_file)) {
    return true;
  }
  return extract_unity_texture_bundle_to_png(ab_file, png_file);
}

} // namespace maiconv
