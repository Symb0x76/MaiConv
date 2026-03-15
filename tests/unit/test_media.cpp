#include "maiconv/core/media.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {
std::optional<std::string> read_env_var(const char *name) {
#if defined(_WIN32)
  char *value = nullptr;
  std::size_t value_len = 0;
  const errno_t rc = _dupenv_s(&value, &value_len, name);
  if (rc != 0 || value == nullptr) {
    return std::nullopt;
  }
  std::string out(value);
  std::free(value);
  return out;
#else
  const char *value = std::getenv(name);
  if (value == nullptr) {
    return std::nullopt;
  }
  return std::string(value);
#endif
}

class ScopedEnvVar {
public:
  ScopedEnvVar(const std::string &name, const std::string &value)
      : name_(name) {
    const auto existing = read_env_var(name_.c_str());
    if (existing.has_value()) {
      had_existing_ = true;
      existing_value_ = *existing;
    }
    set(value);
  }

  ~ScopedEnvVar() {
    if (had_existing_) {
      set(existing_value_);
    } else {
      unset();
    }
  }

  ScopedEnvVar(const ScopedEnvVar &) = delete;
  ScopedEnvVar &operator=(const ScopedEnvVar &) = delete;

private:
  void set(const std::string &value) {
#if defined(_WIN32)
    _putenv_s(name_.c_str(), value.c_str());
#else
    setenv(name_.c_str(), value.c_str(), 1);
#endif
  }

  void unset() {
#if defined(_WIN32)
    _putenv_s(name_.c_str(), "");
#else
    unsetenv(name_.c_str());
#endif
  }

  std::string name_;
  bool had_existing_ = false;
  std::string existing_value_;
};

std::string trim_quotes(const std::string &text) {
  if (text.size() >= 2U && text.front() == '"' && text.back() == '"') {
    return text.substr(1U, text.size() - 2U);
  }
  return text;
}

std::optional<fs::path> find_ffmpeg_path_from_env() {
  const auto path_env = read_env_var("PATH");
  if (!path_env.has_value() || path_env->empty()) {
    return std::nullopt;
  }

#if defined(_WIN32)
  constexpr char kSeparator = ';';
  const fs::path kName = "ffmpeg.exe";
#else
  constexpr char kSeparator = ':';
  const fs::path kName = "ffmpeg";
#endif

  const std::string raw = *path_env;
  std::size_t begin = 0U;
  while (begin <= raw.size()) {
    const std::size_t end = raw.find(kSeparator, begin);
    const std::string entry = trim_quotes(raw.substr(
        begin, end == std::string::npos ? std::string::npos : end - begin));
    if (!entry.empty()) {
      const fs::path candidate = fs::path(entry) / kName;
      std::error_code ec;
      if (fs::exists(candidate, ec) && fs::is_regular_file(candidate, ec)) {
        return candidate;
      }
    }
    if (end == std::string::npos) {
      break;
    }
    begin = end + 1U;
  }

  return std::nullopt;
}

bool is_ffmpeg_available() {
#if defined(_WIN32)
  const int rc = std::system("ffmpeg -version >nul 2>nul");
#else
  const int rc = std::system("ffmpeg -version >/dev/null 2>/dev/null");
#endif
  return rc == 0;
}

bool create_tiny_mp4_sample(const fs::path &output_mp4) {
  if (output_mp4.has_parent_path()) {
    std::error_code ec;
    fs::create_directories(output_mp4.parent_path(), ec);
  }

#if defined(_WIN32)
  const std::string cmd = "ffmpeg -y -loglevel error -f lavfi -i "
                          "color=c=black:s=16x16:d=0.2 -an -c:v libx264 \"" +
                          output_mp4.string() + "\"";
#else
  const std::string cmd = "ffmpeg -y -loglevel error -f lavfi -i "
                          "color=c=black:s=16x16:d=0.2 -an -c:v libx264 \"" +
                          output_mp4.string() + "\"";
#endif
  return std::system(cmd.c_str()) == 0 && fs::exists(output_mp4) &&
         fs::file_size(output_mp4) > 0;
}

bool create_tiny_mpeg1_es_sample(const fs::path &output_m1v) {
  if (output_m1v.has_parent_path()) {
    std::error_code ec;
    fs::create_directories(output_m1v.parent_path(), ec);
  }

  const std::string cmd =
      "ffmpeg -y -loglevel error -f lavfi -i "
      "color=c=black:s=16x16:d=0.2 -an -c:v mpeg1video -f mpeg1video \"" +
      output_m1v.string() + "\"";
  return std::system(cmd.c_str()) == 0 && fs::exists(output_m1v) &&
         fs::file_size(output_m1v) > 0;
}

bool read_binary_payload(const fs::path &path,
                         std::vector<unsigned char> &out) {
  out.clear();
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return false;
  }
  in.seekg(0, std::ios::end);
  const auto size = in.tellg();
  if (size <= 0) {
    return false;
  }
  in.seekg(0, std::ios::beg);
  out.resize(static_cast<std::size_t>(size));
  in.read(reinterpret_cast<char *>(out.data()),
          static_cast<std::streamsize>(out.size()));
  return in.gcount() == static_cast<std::streamsize>(out.size());
}

bool create_minimal_crid_usm_with_payload(
    const fs::path &output_dat, const std::vector<unsigned char> &payload) {
  if (payload.empty()) {
    return false;
  }

  std::ofstream out(output_dat, std::ios::binary | std::ios::trunc);
  if (!out) {
    return false;
  }

  {
    std::array<unsigned char, 0x20> header{};
    header[0] = static_cast<unsigned char>('C');
    header[1] = static_cast<unsigned char>('R');
    header[2] = static_cast<unsigned char>('I');
    header[3] = static_cast<unsigned char>('D');
    header[7] = 0x18U; // chunk size after header: 24 (metadata-only chunk)
    header[9] = 0x18U; // payload offset from chunk header
    header[15] = 0x01U;
    out.write(reinterpret_cast<const char *>(header.data()),
              static_cast<std::streamsize>(header.size()));
  }

  constexpr std::size_t kMaxPlainPayloadPerChunk = 0x200U;
  std::size_t written = 0U;
  while (written < payload.size()) {
    const std::size_t this_chunk_size =
        std::min(kMaxPlainPayloadPerChunk, payload.size() - written);

    std::array<unsigned char, 0x20> header{};
    header[0] = static_cast<unsigned char>('@');
    header[1] = static_cast<unsigned char>('S');
    header[2] = static_cast<unsigned char>('F');
    header[3] = static_cast<unsigned char>('V');
    const uint32_t chunk_size_after_header =
        static_cast<uint32_t>(this_chunk_size);
    header[4] =
        static_cast<unsigned char>((chunk_size_after_header >> 24U) & 0xFFU);
    header[5] =
        static_cast<unsigned char>((chunk_size_after_header >> 16U) & 0xFFU);
    header[6] =
        static_cast<unsigned char>((chunk_size_after_header >> 8U) & 0xFFU);
    header[7] = static_cast<unsigned char>(chunk_size_after_header & 0xFFU);
    header[9] = 0U;
    header[12] = 0U;
    header[15] = 0U;

    out.write(reinterpret_cast<const char *>(header.data()),
              static_cast<std::streamsize>(header.size()));
    out.write(reinterpret_cast<const char *>(
                  payload.data() + static_cast<std::ptrdiff_t>(written)),
              static_cast<std::streamsize>(this_chunk_size));
    written += this_chunk_size;
  }

  out.flush();
  return static_cast<bool>(out);
}

} // namespace

TEST_CASE("media conversion fails cleanly for missing input files") {
  const fs::path temp_root =
      fs::temp_directory_path() / "maiconv_media_missing_input";
  std::error_code ec;
  fs::remove_all(temp_root, ec);
  fs::create_directories(temp_root, ec);

  const fs::path missing = temp_root / "missing.bin";
  const fs::path missing_acb = temp_root / "music.acb";
  const fs::path missing_awb = temp_root / "music.awb";

  REQUIRE_FALSE(
      maiconv::convert_audio_to_mp3(missing, temp_root / "track.mp3"));
  REQUIRE_FALSE(maiconv::convert_acb_awb_to_mp3(missing_acb, missing_awb,
                                                temp_root / "track.mp3"));
  REQUIRE_FALSE(maiconv::convert_ab_to_png(missing, temp_root / "bg.png"));
  REQUIRE_FALSE(
      maiconv::convert_dat_or_usm_to_mp4(missing, temp_root / "pv.mp4"));

  fs::remove_all(temp_root, ec);
}

TEST_CASE("media rejects non-container inputs for video conversion") {
  const fs::path temp_root =
      fs::temp_directory_path() / "maiconv_media_reject_non_vp9";
  std::error_code ec;
  fs::remove_all(temp_root, ec);
  fs::create_directories(temp_root, ec);

  const fs::path fake_mp4 = temp_root / "fake.mp4";
  const fs::path fake_h264 = temp_root / "fake_h264.dat";
  const fs::path output_mp4 = temp_root / "pv.mp4";

  {
    std::ofstream out(fake_mp4, std::ios::binary);
    // Minimal MP4-like header (ftyp) used to guard against passthrough
    // regressions.
    const unsigned char header[] = {0x00U,
                                    0x00U,
                                    0x00U,
                                    0x18U,
                                    static_cast<unsigned char>('f'),
                                    static_cast<unsigned char>('t'),
                                    static_cast<unsigned char>('y'),
                                    static_cast<unsigned char>('p'),
                                    static_cast<unsigned char>('i'),
                                    static_cast<unsigned char>('s'),
                                    static_cast<unsigned char>('o'),
                                    static_cast<unsigned char>('m')};
    out.write(reinterpret_cast<const char *>(header),
              static_cast<std::streamsize>(sizeof(header)));
  }

  {
    std::ofstream out(fake_h264, std::ios::binary);
    // Annex-B start code + SPS NAL marker to guard against accidental H264 path
    // reintroduction.
    const unsigned char header[] = {0x00U, 0x00U, 0x00U, 0x01U,
                                    0x67U, 0x42U, 0x00U, 0x1EU};
    out.write(reinterpret_cast<const char *>(header),
              static_cast<std::streamsize>(sizeof(header)));
  }

  REQUIRE_FALSE(maiconv::convert_dat_or_usm_to_mp4(fake_mp4, output_mp4));
  REQUIRE_FALSE(maiconv::convert_dat_or_usm_to_mp4(fake_h264, output_mp4));

  fs::remove_all(temp_root, ec);
}

TEST_CASE("media converts CRID-wrapped MPEG video stream to mp4") {
  if (!is_ffmpeg_available()) {
    SKIP("ffmpeg not found in PATH");
  }

  const fs::path temp_root =
      fs::temp_directory_path() / "maiconv_media_crid_mpeg_to_mp4";
  std::error_code ec;
  fs::remove_all(temp_root, ec);
  fs::create_directories(temp_root, ec);

  const fs::path source_m1v = temp_root / "source.m1v";
  REQUIRE(create_tiny_mpeg1_es_sample(source_m1v));

  std::vector<unsigned char> payload;
  REQUIRE(read_binary_payload(source_m1v, payload));

  const fs::path source_dat = temp_root / "source.dat";
  REQUIRE(create_minimal_crid_usm_with_payload(source_dat, payload));

  const fs::path output_mp4 = temp_root / "output.mp4";
  REQUIRE(maiconv::convert_dat_or_usm_to_mp4(source_dat, output_mp4));
  REQUIRE(fs::exists(output_mp4));
  REQUIRE(fs::file_size(output_mp4) > 0U);

  fs::remove_all(temp_root, ec);
}

TEST_CASE("media copy mp3 without transcoder") {
  const fs::path temp_root =
      fs::temp_directory_path() / "maiconv_media_copy_mp3";
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

TEST_CASE("media can package mp3 to acb+awb and roundtrip back to mp3") {
  const fs::path temp_root =
      fs::temp_directory_path() / "maiconv_media_mp3_acb_awb_roundtrip";
  std::error_code ec;
  fs::remove_all(temp_root, ec);
  fs::create_directories(temp_root, ec);

  const fs::path src_mp3 = temp_root / "input.mp3";
  const fs::path out_acb = temp_root / "track.acb";
  const fs::path out_awb = temp_root / "track.awb";
  const fs::path out_mp3 = temp_root / "output.mp3";

  {
    std::ofstream out(src_mp3, std::ios::binary | std::ios::trunc);
    const std::array<unsigned char, 16> fake_mp3 = {
        static_cast<unsigned char>('I'),
        static_cast<unsigned char>('D'),
        static_cast<unsigned char>('3'),
        0x04U,
        0x00U,
        0x00U,
        0x00U,
        0x00U,
        0x00U,
        0x00U,
        0xFFU,
        0xFBU,
        0x90U,
        0x64U,
        0x00U,
        0x00U};
    out.write(reinterpret_cast<const char *>(fake_mp3.data()),
              static_cast<std::streamsize>(fake_mp3.size()));
  }

  REQUIRE(maiconv::convert_mp3_to_acb_awb(src_mp3, out_acb, out_awb));
  REQUIRE(fs::exists(out_acb));
  REQUIRE(fs::exists(out_awb));
  REQUIRE(fs::file_size(out_awb) == fs::file_size(src_mp3));

  REQUIRE(maiconv::convert_acb_awb_to_mp3(out_acb, out_awb, out_mp3));
  REQUIRE(fs::exists(out_mp3));
  REQUIRE(fs::file_size(out_mp3) == fs::file_size(src_mp3));

  fs::remove_all(temp_root, ec);
}

TEST_CASE("media mp4->dat works without template using built-in packer") {
  if (!is_ffmpeg_available()) {
    SKIP("ffmpeg not found in PATH");
  }

  const fs::path temp_root =
      fs::temp_directory_path() / "maiconv_media_mp4_to_dat_no_template";
  std::error_code ec;
  fs::remove_all(temp_root, ec);
  fs::create_directories(temp_root, ec);

  const fs::path input_mp4 = temp_root / "input.mp4";
  REQUIRE(create_tiny_mp4_sample(input_mp4));

  const fs::path output_dat = temp_root / "pv.dat";
  REQUIRE(maiconv::convert_mp4_to_dat(input_mp4, output_dat));
  REQUIRE(fs::exists(output_dat));
  REQUIRE(fs::file_size(output_dat) > 0U);

  const fs::path roundtrip_mp4 = temp_root / "roundtrip.mp4";
  REQUIRE(maiconv::convert_dat_or_usm_to_mp4(output_dat, roundtrip_mp4));
  REQUIRE(fs::exists(roundtrip_mp4));
  REQUIRE(fs::file_size(roundtrip_mp4) > 0U);

  fs::remove_all(temp_root, ec);
}

TEST_CASE("media mp4->dat supports MAICONV_FFMPEG absolute path override") {
  if (!is_ffmpeg_available()) {
    SKIP("ffmpeg not found in PATH");
  }

  const auto ffmpeg_path = find_ffmpeg_path_from_env();
  if (!ffmpeg_path.has_value()) {
    SKIP("cannot resolve ffmpeg absolute path from PATH");
  }

  const fs::path temp_root =
      fs::temp_directory_path() / "maiconv_media_ffmpeg_env_override";
  std::error_code ec;
  fs::remove_all(temp_root, ec);
  fs::create_directories(temp_root, ec);

  const fs::path input_mp4 = temp_root / "input.mp4";
  REQUIRE(create_tiny_mp4_sample(input_mp4));

  const ScopedEnvVar scoped_ffmpeg("MAICONV_FFMPEG", ffmpeg_path->string());
  const fs::path output_dat = temp_root / "pv.dat";
  REQUIRE(maiconv::convert_mp4_to_dat(input_mp4, output_dat));
  REQUIRE(fs::exists(output_dat));
  REQUIRE(fs::file_size(output_dat) > 0U);

  fs::remove_all(temp_root, ec);
}

TEST_CASE(
    "media mp4->dat invalid MAICONV_FFMPEG override follows backend behavior") {
  if (!is_ffmpeg_available()) {
    SKIP("ffmpeg not found in PATH");
  }

  const fs::path temp_root =
      fs::temp_directory_path() / "maiconv_media_ffmpeg_env_invalid";
  std::error_code ec;
  fs::remove_all(temp_root, ec);
  fs::create_directories(temp_root, ec);

  const fs::path input_mp4 = temp_root / "input.mp4";
  REQUIRE(create_tiny_mp4_sample(input_mp4));

  const ScopedEnvVar scoped_ffmpeg("MAICONV_FFMPEG",
                                   (temp_root / "missing_ffmpeg.exe").string());
  REQUIRE_FALSE(maiconv::convert_mp4_to_dat(input_mp4, temp_root / "pv.dat"));

  fs::remove_all(temp_root, ec);
}

TEST_CASE("media extracts embedded png from mock .ab payload") {
  const fs::path temp_root =
      fs::temp_directory_path() / "maiconv_media_mock_ab_to_png";
  std::error_code ec;
  fs::remove_all(temp_root, ec);
  fs::create_directories(temp_root, ec);

  const fs::path mock_ab = temp_root / "bg.ab";
  const fs::path output_png = temp_root / "bg.png";

  // Structurally minimal PNG bytes (IHDR + IEND) for extractor-path testing.
  const std::vector<unsigned char> kMinimalPng = {
      0x89U, 0x50U, 0x4EU, 0x47U, 0x0DU, 0x0AU, 0x1AU, 0x0AU, 0x00U,
      0x00U, 0x00U, 0x0DU, 0x49U, 0x48U, 0x44U, 0x52U, 0x00U, 0x00U,
      0x00U, 0x01U, 0x00U, 0x00U, 0x00U, 0x01U, 0x08U, 0x02U, 0x00U,
      0x00U, 0x00U, 0x90U, 0x77U, 0x53U, 0xDEU, 0x00U, 0x00U, 0x00U,
      0x00U, 0x49U, 0x45U, 0x4EU, 0x44U, 0xAEU, 0x42U, 0x60U, 0x82U};

  {
    std::ofstream out(mock_ab, std::ios::binary | std::ios::trunc);
    const std::array<unsigned char, 4> junk = {0x11U, 0x22U, 0x33U, 0x44U};
    out.write(reinterpret_cast<const char *>(junk.data()),
              static_cast<std::streamsize>(junk.size()));
    out.write(reinterpret_cast<const char *>(kMinimalPng.data()),
              static_cast<std::streamsize>(kMinimalPng.size()));
  }

  REQUIRE(maiconv::convert_ab_to_png(mock_ab, output_png));
  REQUIRE(fs::exists(output_png));
  REQUIRE(fs::file_size(output_png) == kMinimalPng.size());

  std::array<unsigned char, 8> head{};
  {
    std::ifstream in(output_png, std::ios::binary);
    REQUIRE(static_cast<bool>(in));
    in.read(reinterpret_cast<char *>(head.data()),
            static_cast<std::streamsize>(head.size()));
    REQUIRE(in.gcount() == static_cast<std::streamsize>(head.size()));
  }
  const std::array<unsigned char, 8> png_sig = {0x89U, 0x50U, 0x4EU, 0x47U,
                                                0x0DU, 0x0AU, 0x1AU, 0x0AU};
  REQUIRE(head == png_sig);

  fs::remove_all(temp_root, ec);
}
