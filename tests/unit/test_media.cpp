#include "maiconv/core/media.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

TEST_CASE("media conversion fails cleanly for missing input files") {
  const fs::path temp_root = fs::temp_directory_path() / "maiconv_media_missing_input";
  std::error_code ec;
  fs::remove_all(temp_root, ec);
  fs::create_directories(temp_root, ec);

  const fs::path missing = temp_root / "missing.bin";
  const fs::path missing_acb = temp_root / "music.acb";
  const fs::path missing_awb = temp_root / "music.awb";

  REQUIRE_FALSE(maiconv::convert_audio_to_mp3(missing, temp_root / "track.mp3"));
  REQUIRE_FALSE(maiconv::convert_acb_awb_to_mp3(missing_acb, missing_awb, temp_root / "track.mp3"));
  REQUIRE_FALSE(maiconv::convert_ab_to_png(missing, temp_root / "bg.png"));
  REQUIRE_FALSE(maiconv::convert_dat_or_usm_to_mp4(missing, temp_root / "pv.mp4"));

  fs::remove_all(temp_root, ec);
}

TEST_CASE("media copy mp3 without transcoder") {
  const fs::path temp_root = fs::temp_directory_path() / "maiconv_media_copy_mp3";
  std::error_code ec;
  fs::remove_all(temp_root, ec);
  fs::create_directories(temp_root, ec);

  const fs::path src = temp_root / "source.mp3";
  const fs::path dst = temp_root / "out.mp3";

  {
    std::ofstream out(src, std::ios::binary);
    out << "dummy_mp3";
  }

  REQUIRE(maiconv::convert_audio_to_mp3(src, dst));
  REQUIRE(fs::exists(dst));
  REQUIRE(fs::file_size(dst) == fs::file_size(src));

  fs::remove_all(temp_root, ec);
}

TEST_CASE("media converts real usm/dat sample to mp4") {
  const fs::path sample = fs::path("tests") / "StreamingAssets" / "A045" / "MovieData" / "001944.dat";
  if (!fs::exists(sample)) {
    SKIP("sample dat not found in test workspace");
  }

  const fs::path temp_root = fs::temp_directory_path() / "maiconv_media_usm_to_mp4";
  std::error_code ec;
  fs::remove_all(temp_root, ec);
  fs::create_directories(temp_root, ec);

  const fs::path output = temp_root / "pv.mp4";
  REQUIRE(maiconv::convert_dat_or_usm_to_mp4(sample, output));
  REQUIRE(fs::exists(output));
  REQUIRE(fs::file_size(output) > 1024);

  std::array<unsigned char, 12> head{};
  {
    std::ifstream in(output, std::ios::binary);
    REQUIRE(static_cast<bool>(in));
    in.read(reinterpret_cast<char*>(head.data()), static_cast<std::streamsize>(head.size()));
    REQUIRE(in.gcount() == static_cast<std::streamsize>(head.size()));
  }

  REQUIRE(head[4] == static_cast<unsigned char>('f'));
  REQUIRE(head[5] == static_cast<unsigned char>('t'));
  REQUIRE(head[6] == static_cast<unsigned char>('y'));
  REQUIRE(head[7] == static_cast<unsigned char>('p'));

  fs::remove_all(temp_root, ec);
}
