#include "maiconv/core/unity_assetbundle_internal.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

TEST_CASE("texture dimensions reject values that drive huge allocations") {
  // Decoders size buffers as width * height * 4, so a 4-byte header field of
  // UINT32_MAX asks for roughly 17 GB.
  REQUIRE_FALSE(maiconv::texture_dimensions_plausible(0xFFFFFFFFU, 1U));
  REQUIRE_FALSE(maiconv::texture_dimensions_plausible(1U, 0xFFFFFFFFU));

  // UINT32_MAX is also the value that defeats the decoders' own size guard:
  // (0xFFFFFFFF + 3) / 4 wraps to 0 in uint32_t, making the expected block
  // size 0 so that any payload, however short, passes the length check.
  const uint32_t wrapped_blocks = static_cast<uint32_t>(0xFFFFFFFFU + 3U) / 4U;
  REQUIRE(wrapped_blocks == 0U);

  // Zero is rejected too: it yields an empty buffer and a 0x0 PNG.
  REQUIRE_FALSE(maiconv::texture_dimensions_plausible(0U, 64U));
  REQUIRE_FALSE(maiconv::texture_dimensions_plausible(64U, 0U));

  // Just over the cap fails, exactly at the cap passes.
  REQUIRE_FALSE(maiconv::texture_dimensions_plausible(
      maiconv::kMaxTextureDimension + 1U, 64U));
  REQUIRE(maiconv::texture_dimensions_plausible(maiconv::kMaxTextureDimension,
                                                maiconv::kMaxTextureDimension));

  // Ordinary jacket sizes are unaffected.
  REQUIRE(maiconv::texture_dimensions_plausible(512U, 512U));
  REQUIRE(maiconv::texture_dimensions_plausible(1920U, 1080U));
}

TEST_CASE("texture stream path cannot escape the bundle directory") {
#if defined(_WIN32)
  const fs::path bundle_dir = "C:/assets/jacket";
#else
  const fs::path bundle_dir = "/assets/jacket";
#endif

  // An absolute path is the dangerous case: std::filesystem::operator/
  // discards the left operand entirely when the right one is absolute, so
  // without this check the file would be read verbatim and its bytes baked
  // into the output PNG. A leading slash carries a root directory on both
  // platforms, so this form is rejected everywhere.
  REQUIRE_FALSE(
      maiconv::resolve_contained_stream_path(bundle_dir, "/etc/passwd")
          .has_value());

#if defined(_WIN32)
  // A drive letter is only a root name on Windows. On POSIX "C:/..." is an
  // ordinary relative name and resolves harmlessly inside the bundle
  // directory, so this case is asserted only where it can actually escape.
  REQUIRE_FALSE(maiconv::resolve_contained_stream_path(
                    bundle_dir, "C:/Users/someone/.ssh/id_rsa")
                    .has_value());
  REQUIRE_FALSE(
      maiconv::resolve_contained_stream_path(bundle_dir, "\\\\server\\share\\x")
          .has_value());
#endif

  // Parent-directory traversal, plain and buried mid-path.
  REQUIRE_FALSE(
      maiconv::resolve_contained_stream_path(bundle_dir, "../../secret.bin")
          .has_value());
  REQUIRE_FALSE(maiconv::resolve_contained_stream_path(
                    bundle_dir, "sub/../../../secret.bin")
                    .has_value());

  // Empty path names nothing.
  REQUIRE_FALSE(
      maiconv::resolve_contained_stream_path(bundle_dir, "").has_value());

  // Legitimate sibling and nested resource paths still resolve, and resolve
  // underneath the bundle directory.
  const auto sibling =
      maiconv::resolve_contained_stream_path(bundle_dir, "jacket.resS");
  REQUIRE(sibling.has_value());
  REQUIRE(*sibling == bundle_dir / "jacket.resS");

  const auto nested = maiconv::resolve_contained_stream_path(
      bundle_dir, "sharedassets/jacket.resS");
  REQUIRE(nested.has_value());
  REQUIRE(nested->parent_path() == bundle_dir / "sharedassets");
}
