#include "maiconv/core/media/media_shared.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

TEST_CASE("media_shared byte-order readers are endian-correct") {
  const std::array<uint8_t, 4> be32 = {0x12U, 0x34U, 0x56U, 0x78U};
  REQUIRE(maiconv::media_shared_read_u32_be(be32.data()) == 0x12345678U);

  const std::array<uint8_t, 2> le16 = {0x34U, 0x12U};
  REQUIRE(maiconv::media_shared_read_u16_le(le16.data()) == 0x1234U);

  const std::array<uint8_t, 2> be16 = {0x12U, 0x34U};
  REQUIRE(maiconv::media_shared_read_u16_be(be16.data()) == 0x1234U);
}

TEST_CASE("media_shared byte-order readers distinguish endianness") {
  const std::array<uint8_t, 2> pair = {0x01U, 0x80U};
  REQUIRE(maiconv::media_shared_read_u16_le(pair.data()) == 0x8001U);
  REQUIRE(maiconv::media_shared_read_u16_be(pair.data()) == 0x0180U);
}

TEST_CASE("media_shared read_exact reads and detects short input") {
  std::istringstream short_in("ab");
  std::array<uint8_t, 4> buf{};
  REQUIRE_FALSE(
      maiconv::media_shared_read_exact(short_in, buf.data(), buf.size()));

  std::istringstream full_in("abcdefgh");
  std::array<uint8_t, 4> full_buf{};
  REQUIRE(maiconv::media_shared_read_exact(full_in, full_buf.data(),
                                           full_buf.size()));
  REQUIRE(full_buf[0] == static_cast<uint8_t>('a'));
  REQUIRE(full_buf[3] == static_cast<uint8_t>('d'));
}

TEST_CASE("media_shared is_mp3_like_file detects mp3 signatures") {
  const fs::path temp_root =
      fs::temp_directory_path() / "maiconv_media_shared_mp3_probe";
  std::error_code ec;
  fs::remove_all(temp_root, ec);
  fs::create_directories(temp_root, ec);

  const fs::path id3 = temp_root / "id3.bin";
  {
    std::ofstream out(id3, std::ios::binary);
    out.write("ID3\x04\x00\x00\x00\x00\x00\x00", 10);
  }
  REQUIRE(maiconv::media_shared_is_mp3_like_file(id3));

  const fs::path mpeg = temp_root / "mpeg.bin";
  {
    std::ofstream out(mpeg, std::ios::binary);
    // MPEG frame sync: 0xFF 0xFB.
    out.write("\xFF\xFB\x90\x00", 4);
  }
  REQUIRE(maiconv::media_shared_is_mp3_like_file(mpeg));

  const fs::path not_mp3 = temp_root / "notmp3.bin";
  {
    std::ofstream out(not_mp3, std::ios::binary);
    out.write("OggS", 4);
  }
  REQUIRE_FALSE(maiconv::media_shared_is_mp3_like_file(not_mp3));

  const fs::path missing = temp_root / "missing.bin";
  REQUIRE_FALSE(maiconv::media_shared_is_mp3_like_file(missing));

  fs::remove_all(temp_root, ec);
}

TEST_CASE("media_shared is_mp3_like_file accepts short sync at EOF") {
  const fs::path temp_root =
      fs::temp_directory_path() / "maiconv_media_shared_mp3_short";
  std::error_code ec;
  fs::remove_all(temp_root, ec);
  fs::create_directories(temp_root, ec);

  // Two-byte head is enough for the MPEG frame check.
  const fs::path two_byte = temp_root / "two.bin";
  {
    std::ofstream out(two_byte, std::ios::binary);
    out.write("\xFF\xE0", 2);
  }
  REQUIRE(maiconv::media_shared_is_mp3_like_file(two_byte));

  fs::remove_all(temp_root, ec);
}

TEST_CASE("media_shared acb stub read rejects non-stub and missing files") {
  const fs::path temp_root =
      fs::temp_directory_path() / "maiconv_media_shared_acb_stub";
  std::error_code ec;
  fs::remove_all(temp_root, ec);
  fs::create_directories(temp_root, ec);

  const fs::path plain = temp_root / "plain.acb";
  {
    std::ofstream out(plain, std::ios::binary);
    out.write("not-a-stub", 10);
  }
  std::string name;
  uint64_t size = 0;
  REQUIRE_FALSE(
      maiconv::media_shared_read_acb_stub_sidecar_awb_name(plain, name, size));

  const fs::path missing = temp_root / "missing.acb";
  REQUIRE_FALSE(maiconv::media_shared_read_acb_stub_sidecar_awb_name(
      missing, name, size));

  fs::remove_all(temp_root, ec);
}
