#pragma once
// Internal to MaiConv_core AssetBundle decoding. NOT the public
// media_cover.hpp.
//
// These are the validation predicates applied to Texture2D header fields
// before anything is sized or opened from them. They live here, rather than in
// the anonymous namespace of unity_assetbundle.cpp, so that malformed-input
// cases can be tested without constructing a valid UnityFS container.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace maiconv {

// Largest width or height accepted from a Texture2D header. Real maimai
// jackets and movie frames are far below this; the cap exists only so that an
// attacker-controlled 4-byte field cannot drive an allocation.
inline constexpr uint32_t kMaxTextureDimension = 16384U;

// Hosted in unity_assetbundle.cpp.
// Rejects zero and implausibly large dimensions. Decoders size buffers as
// width * height * 4 and compute block counts as (dimension + 3) / 4 in
// uint32_t, which wraps to 0 near UINT32_MAX and makes their own size guards
// pass for any payload.
bool texture_dimensions_plausible(uint32_t width, uint32_t height);

// Hosted in unity_assetbundle.cpp.
// Resolves a Texture2D m_StreamData path against the directory holding the
// bundle. Returns nullopt when the path is empty, absolute, or contains a
// parent-directory component, i.e. whenever it would name a file outside the
// bundle's own directory.
//
// This is a lexical check. It does not resolve symlinks, which would require
// an attacker to already have write access beside the bundle -- a different
// and much higher privilege than supplying a malicious bundle.
std::optional<std::filesystem::path>
resolve_contained_stream_path(const std::filesystem::path &bundle_dir,
                              const std::string &stream_path);

} // namespace maiconv
