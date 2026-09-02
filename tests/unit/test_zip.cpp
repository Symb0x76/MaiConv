#include "maiconv/core/zip_util.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {
fs::path unique_temp_dir(const std::string &tag) {
  const auto now =
      std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::high_resolution_clock::now().time_since_epoch())
          .count();
  const fs::path dir = fs::temp_directory_path() /
                       ("maiconv_zip_test_" + tag + "_" + std::to_string(now));
  fs::create_directories(dir);
  return dir;
}

void write_text_file(const fs::path &path, const std::string &content) {
  std::ofstream out(path, std::ios::binary);
  out.write(content.data(), static_cast<std::streamsize>(content.size()));
}

bool file_starts_with(const fs::path &path,
                      const std::array<uint8_t, 4> &magic) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return false;
  }
  std::array<uint8_t, 4> head{};
  in.read(reinterpret_cast<char *>(head.data()),
          static_cast<std::streamsize>(head.size()));
  return in.gcount() == static_cast<std::streamsize>(head.size()) &&
         head == magic;
}
} // namespace

TEST_CASE("zip zips folder contents and removes the folder") {
  const fs::path temp_root = unique_temp_dir("basic");
  const fs::path folder = temp_root / "track";
  fs::create_directories(folder / "sub");
  write_text_file(folder / "maidata.txt", "&inote_1=\n");
  write_text_file(folder / "track.mp3", "fake-mp3-data");
  write_text_file(folder / "sub" / "nested.bin", "nested");

  REQUIRE(maiconv::zip_folder_and_remove(folder));
  REQUIRE(!fs::exists(folder));
  const fs::path zip_path = temp_root / "track.zip";
  REQUIRE(fs::is_regular_file(zip_path));
  REQUIRE(fs::file_size(zip_path) > 0);

  // Local file header signature at offset 0.
  REQUIRE(file_starts_with(zip_path, {0x50U, 0x4BU, 0x03U, 0x04U}));

  fs::remove_all(temp_root);
}

TEST_CASE("zip refuses non-directory input") {
  const fs::path temp_root = unique_temp_dir("notdir");
  write_text_file(temp_root / "file.txt", "x");
  REQUIRE(!maiconv::zip_folder_and_remove(temp_root / "file.txt"));
  fs::remove_all(temp_root);
}

TEST_CASE("zip refuses when target zip already exists") {
  const fs::path temp_root = unique_temp_dir("exists");
  const fs::path folder = temp_root / "track";
  fs::create_directories(folder);
  write_text_file(folder / "a.txt", "a");
  write_text_file(temp_root / "track.zip", "existing");

  REQUIRE(!maiconv::zip_folder_and_remove(folder));
  REQUIRE(fs::exists(folder));

  fs::remove_all(temp_root);
}
