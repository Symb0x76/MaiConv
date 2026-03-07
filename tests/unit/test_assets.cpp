#include "maiconv/core/assets.hpp"
#include "maiconv/core/io.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

using namespace maiconv;

namespace {

namespace fs = std::filesystem;

fs::path unique_temp_dir(const std::string& prefix) {
  const auto stamp = std::chrono::duration_cast<std::chrono::microseconds>(
                         std::chrono::high_resolution_clock::now().time_since_epoch())
                         .count();
  const fs::path dir = fs::temp_directory_path() / ("maiconv_" + prefix + "_" + std::to_string(stamp));
  fs::create_directories(dir);
  return dir;
}

std::string sample_ma2() {
  return "VERSION\t0.00.00\t1.04.00\n"
         "FES_MODE\t0\n"
         "BPM_DEF\t120.000\t120.000\t120.000\t120.000\n"
         "MET_DEF\t4\t4\n"
         "RESOLUTION\t384\n"
         "CLK_DEF\t384\n"
         "COMPATIBLE_CODE\tMA2\n"
         "\n"
         "BPM\t0\t0\t120\n"
         "MET\t0\t0\t4\t4\n"
         "\n"
         "NMTAP\t0\t0\t0\n";
}

std::string sample_music_xml(const std::string& id6,
                             const std::string& title,
                             const std::string& genre,
                             const std::string& version) {
  return "<MusicData>\n"
         "  <id><str>" + id6 + "</str></id>\n"
         "  <name><str>" + title + "</str></name>\n"
         "  <sortName><str>" + title + "</str></sortName>\n"
         "  <genreName><str>" + genre + "</str></genreName>\n"
         "  <version><str>" + version + "</str></version>\n"
         "  <artistName><str>Tester</str></artistName>\n"
         "  <bpm><str>120</str></bpm>\n"
         "</MusicData>\n";
}

void create_track(const fs::path& db_root,
                  const std::string& id6,
                  const std::string& title,
                  const std::string& genre,
                  const std::string& version,
                  const std::vector<int>& difficulties = {3}) {
  const fs::path track_folder = db_root / "music" / ("music" + id6);
  fs::create_directories(track_folder);
  for (const int diff : difficulties) {
    const std::string suffix = diff < 10 ? "0" + std::to_string(diff) : std::to_string(diff);
    write_text_file(track_folder / (id6 + "_" + suffix + ".ma2"), sample_ma2());
  }
  write_text_file(track_folder / "Music.xml", sample_music_xml(id6, title, genre, version));
}

void create_media_assets(const fs::path& db_root, const std::string& id6) {
  fs::create_directories(db_root / "SoundData");
  fs::create_directories(db_root / "AssetBundleImages");
  fs::create_directories(db_root / "MovieData");

  write_text_file(db_root / "SoundData" / ("music" + id6 + ".mp3"), "dummy");
  write_text_file(db_root / "AssetBundleImages" / ("UI_Jacket_" + id6 + ".png"), "dummy");
  write_text_file(db_root / "MovieData" / (id6 + ".mp4"), "dummy");
}

void create_compact_media_assets(const fs::path& db_root, const std::string& id6) {
  fs::create_directories(db_root / "SoundData");
  fs::create_directories(db_root / "AssetBundleImages" / "jacket");
  fs::create_directories(db_root / "MovieData");

  int id_num = std::stoi(id6);
  if (id_num < 0) {
    id_num = -id_num;
  }
  const std::string non_dx = pad_music_id(std::to_string(id_num % 10000), 6);

  write_text_file(db_root / "SoundData" / ("music" + non_dx + ".acb"), "dummy");
  write_text_file(db_root / "SoundData" / ("music" + non_dx + ".awb"), "dummy");
  write_text_file(db_root / "AssetBundleImages" / "jacket" / ("ui_jacket_" + non_dx + ".ab"), "dummy");
  write_text_file(db_root / "MovieData" / (non_dx + ".dat"), "dummy");
}

}  // namespace

TEST_CASE("assets scans all subdirectories under assets root in flat layout") {
  const fs::path temp_root = unique_temp_dir("assets_scan");
  const fs::path assets_root = temp_root / "StreamingAssets";
  const fs::path output_root = temp_root / "output";

  fs::create_directories(assets_root);
  fs::create_directories(output_root);

  create_track(assets_root / "A000", "000001", "One", "POPS", "PRISM");
  create_track(assets_root / "L100", "000002", "Two", "ANIME", "PRISM");

  AssetsOptions options;
  options.streaming_assets_path = assets_root;
  options.output_path = output_root;
  options.format = ChartFormat::Simai;

  const int rc = run_compile_assets(options);
  REQUIRE(rc == 0);
  REQUIRE(fs::exists(output_root / "000001_One" / "maidata.txt"));
  REQUIRE(fs::exists(output_root / "000002_Two" / "maidata.txt"));

  fs::remove_all(temp_root);
}

TEST_CASE("assets supports genre/version layout") {
  const fs::path temp_root = unique_temp_dir("assets_layout");
  const fs::path assets_root = temp_root / "StreamingAssets";
  const fs::path out_genre = temp_root / "out_genre";
  const fs::path out_version = temp_root / "out_version";

  fs::create_directories(assets_root);
  create_track(assets_root / "A000", "000123", "LayoutSong", "POPS", "BUDDIES");

  AssetsOptions by_genre;
  by_genre.streaming_assets_path = assets_root;
  by_genre.output_path = out_genre;
  by_genre.format = ChartFormat::Ma2_104;
  by_genre.export_layout = AssetsExportLayout::Genre;
  by_genre.music_id_folder_name = true;

  REQUIRE(run_compile_assets(by_genre) == 0);
  REQUIRE(fs::exists(out_genre / "POPS" / "000123" / "result.ma2"));

  AssetsOptions by_version;
  by_version.streaming_assets_path = assets_root;
  by_version.output_path = out_version;
  by_version.format = ChartFormat::Ma2_104;
  by_version.export_layout = AssetsExportLayout::Version;
  by_version.music_id_folder_name = true;

  REQUIRE(run_compile_assets(by_version) == 0);
  REQUIRE(fs::exists(out_version / "BUDDIES" / "000123" / "result.ma2"));

  fs::remove_all(temp_root);
}

TEST_CASE("assets auto-detects media folders from streaming assets roots") {
  const fs::path temp_root = unique_temp_dir("assets_media_detect");
  const fs::path assets_root = temp_root / "StreamingAssets";
  const fs::path output_root = temp_root / "output";

  fs::create_directories(assets_root);
  create_track(assets_root / "A021", "000456", "MediaSong", "VARIETY", "PRISM");
  create_media_assets(assets_root / "A021", "000456");

  AssetsOptions options;
  options.streaming_assets_path = assets_root;
  options.output_path = output_root;
  options.format = ChartFormat::Simai;

  REQUIRE(run_compile_assets(options) == 0);
  REQUIRE(fs::exists(output_root / "000456_MediaSong" / "maidata.txt"));
  REQUIRE(fs::exists(output_root / "000456_MediaSong" / "track.mp3"));
  REQUIRE(fs::exists(output_root / "000456_MediaSong" / "bg.png"));
  REQUIRE(fs::exists(output_root / "000456_MediaSong" / "pv.mp4"));
  REQUIRE_FALSE(fs::exists(output_root / "000456_MediaSong_Incomplete"));

  fs::remove_all(temp_root);
}

TEST_CASE("assets supports acb/awb/ab/dat media naming") {
  const fs::path temp_root = unique_temp_dir("assets_compact_media");
  const fs::path assets_root = temp_root / "StreamingAssets";
  const fs::path output_root = temp_root / "output";

  fs::create_directories(assets_root);
  create_track(assets_root / "A045", "011944", "RawMediaSong", "GAME", "PRISM");
  create_compact_media_assets(assets_root / "A045", "011944");

  AssetsOptions options;
  options.streaming_assets_path = assets_root;
  options.output_path = output_root;
  options.format = ChartFormat::Simai;
  options.ignore_incomplete_assets = true;

  REQUIRE(run_compile_assets(options) == 0);

  const fs::path incomplete_dir = output_root / "011944_RawMediaSong_Incomplete";
  REQUIRE(fs::exists(incomplete_dir / "maidata.txt"));
  REQUIRE_FALSE(fs::exists(incomplete_dir / "track.acb"));
  REQUIRE_FALSE(fs::exists(incomplete_dir / "track.awb"));
  REQUIRE_FALSE(fs::exists(incomplete_dir / "bg.ab"));
  REQUIRE_FALSE(fs::exists(incomplete_dir / "pv.dat"));

  const std::string log = read_text_file(output_root / "_log.txt");
  REQUIRE(log.find("Audio conversion failed") != std::string::npos);
  REQUIRE(log.find("Cover conversion failed") != std::string::npos);
  REQUIRE(log.find("Video conversion failed") != std::string::npos);

  fs::remove_all(temp_root);
}
TEST_CASE("assets exports selected id with all difficulties when difficulty is omitted") {
  const fs::path temp_root = unique_temp_dir("assets_id_all_diff");
  const fs::path assets_root = temp_root / "StreamingAssets";
  const fs::path output_root = temp_root / "output";

  fs::create_directories(assets_root);
  create_track(assets_root / "A000", "000777", "SelectMe", "POPS", "PRISM", {2, 3});
  create_track(assets_root / "A000", "000888", "IgnoreMe", "POPS", "PRISM", {3});

  AssetsOptions options;
  options.streaming_assets_path = assets_root;
  options.output_path = output_root;
  options.format = ChartFormat::Simai;
  options.target_music_id = "777";

  REQUIRE(run_compile_assets(options) == 0);
  REQUIRE(fs::exists(output_root / "000777_SelectMe" / "maidata.txt"));
  REQUIRE_FALSE(fs::exists(output_root / "000888_IgnoreMe"));

  const std::string maidata = read_text_file(output_root / "000777_SelectMe" / "maidata.txt");
  REQUIRE(maidata.find("&inote_2=") != std::string::npos);
  REQUIRE(maidata.find("&inote_3=") != std::string::npos);

  fs::remove_all(temp_root);
}


TEST_CASE("assets uses real song id from Music.xml name/id") {
  const fs::path temp_root = unique_temp_dir("assets_musicxml_real_id");
  const fs::path assets_root = temp_root / "StreamingAssets";
  const fs::path output_root = temp_root / "output";

  const fs::path track_folder = assets_root / "A045" / "music" / "music011944";
  fs::create_directories(track_folder);
  write_text_file(track_folder / "011944_03.ma2", sample_ma2());

  const std::string xml =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
      "<MusicData>\n"
      "  <netOpenName><id>260116</id><str>Net260116</str></netOpenName>\n"
      "  <name><id>11944</id><str>Restricted Access</str></name>\n"
      "  <sortName>RESTRICTEDACCESS</sortName>\n"
      "  <genreName><id>104</id><str>GAME</str></genreName>\n"
      "  <artistName><id>1</id><str>Tester</str></artistName>\n"
      "  <bpm>185</bpm>\n"
      "  <version>25007</version>\n"
      "</MusicData>\n";
  write_text_file(track_folder / "Music.xml", xml);

  AssetsOptions options;
  options.streaming_assets_path = assets_root;
  options.output_path = output_root;
  options.format = ChartFormat::Simai;

  REQUIRE(run_compile_assets(options) == 0);
  REQUIRE(fs::exists(output_root / "011944_Restricted Access" / "maidata.txt"));
  REQUIRE_FALSE(fs::exists(output_root / "260116_Restricted Access"));

  fs::remove_all(temp_root);
}

TEST_CASE("assets preserves utf8 folder names for non-English titles") {
  const fs::path temp_root = unique_temp_dir("assets_utf8_folder");
  const fs::path assets_root = temp_root / "StreamingAssets";
  const fs::path output_root = temp_root / "output";

  const std::u8string title_u8 = u8"胡桃乃舞";
  std::string title;
  title.reserve(title_u8.size());
  for (const char8_t ch : title_u8) {
    title.push_back(static_cast<char>(ch));
  }

  fs::create_directories(assets_root);
  create_track(assets_root / "A000", "000321", title, "POPS", "PRISM");

  AssetsOptions options;
  options.streaming_assets_path = assets_root;
  options.output_path = output_root;
  options.format = ChartFormat::Simai;

  REQUIRE(run_compile_assets(options) == 0);

  const std::string folder_name = "000321_" + sanitize_folder_name(title);
  const fs::path expected_folder = append_utf8_path(output_root, folder_name);
  REQUIRE(fs::exists(expected_folder / "maidata.txt"));

  std::vector<fs::path> exported_dirs;
  for (const auto& entry : fs::directory_iterator(output_root)) {
    if (entry.is_directory()) {
      exported_dirs.push_back(entry.path().filename());
    }
  }
  REQUIRE(exported_dirs.size() == 1);
  REQUIRE(exported_dirs.front() == expected_folder.filename());

  fs::remove_all(temp_root);
}
TEST_CASE("assets exports selected id with selected difficulty only") {
  const fs::path temp_root = unique_temp_dir("assets_id_one_diff");
  const fs::path assets_root = temp_root / "StreamingAssets";
  const fs::path output_root = temp_root / "output";

  fs::create_directories(assets_root);
  create_track(assets_root / "A000", "000999", "DiffSong", "POPS", "PRISM", {2, 3});

  AssetsOptions options;
  options.streaming_assets_path = assets_root;
  options.output_path = output_root;
  options.format = ChartFormat::Simai;
  options.target_music_id = "999";
  options.target_difficulty = 3;

  REQUIRE(run_compile_assets(options) == 0);
  REQUIRE(fs::exists(output_root / "000999_DiffSong" / "maidata.txt"));

  const std::string maidata = read_text_file(output_root / "000999_DiffSong" / "maidata.txt");
  REQUIRE(maidata.find("&inote_3=") != std::string::npos);
  REQUIRE(maidata.find("&inote_2=") == std::string::npos);

  fs::remove_all(temp_root);
}


