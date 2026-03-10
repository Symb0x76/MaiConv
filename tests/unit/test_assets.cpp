#include "maiconv/core/assets.hpp"
#include "maiconv/core/io.hpp"
#include "maiconv/core/simai.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
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

  fs::path repo_test_data_path(const fs::path& relative) {
    return fs::path(__FILE__).parent_path().parent_path() / relative;
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
    const std::vector<int>& difficulties = { 3 }) {
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

  void write_binary_file(const fs::path& path, const std::vector<std::uint8_t>& bytes) {
    if (!path.parent_path().empty()) {
      fs::create_directories(path.parent_path());
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  }

  void create_complete_maidata_fixture_011944(const fs::path& assets_root) {
    const fs::path db_root = assets_root / "A045";
    const fs::path track_folder = db_root / "music" / "music011944";
    fs::create_directories(track_folder);

    write_text_file(track_folder / "011944_00.ma2", sample_ma2());
    write_text_file(track_folder / "011944_01.ma2", sample_ma2());
    write_text_file(track_folder / "011944_02.ma2", sample_ma2());
    write_text_file(track_folder / "011944_03.ma2", sample_ma2());

    const std::string xml =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
      "<MusicData xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\">\n"
      "  <dataName>music011944</dataName>\n"
      "  <netOpenName><id>260116</id><str>Net260116</str></netOpenName>\n"
      "  <releaseTagName><id>5001</id><str>Ver1.50.00</str></releaseTagName>\n"
      "  <disable>false</disable>\n"
      "  <name><id>11944</id><str>Restricted Access</str></name>\n"
      "  <sortName>RESTRICTEDACCESS</sortName>\n"
      "  <artistName><id>1097</id><str>Knighthood</str></artistName>\n"
      "  <genreName><id>104</id><str>ゲームバラエティ</str></genreName>\n"
      "  <bpm>185</bpm>\n"
      "  <version>25007</version>\n"
      "  <AddVersion><id>23</id><str>PRiSM</str></AddVersion>\n"
      "  <notesData>\n"
      "    <Notes><file><path>011944_00.ma2</path></file><level>4</level><levelDecimal>0</levelDecimal><notesDesigner><id>0</id><str /></notesDesigner><notesType>0</notesType><musicLevelID>4</musicLevelID><maxNotes>217</maxNotes><isEnable>true</isEnable></Notes>\n"
      "    <Notes><file><path>011944_01.ma2</path></file><level>7</level><levelDecimal>8</levelDecimal><notesDesigner><id>0</id><str /></notesDesigner><notesType>0</notesType><musicLevelID>8</musicLevelID><maxNotes>389</maxNotes><isEnable>true</isEnable></Notes>\n"
      "    <Notes><file><path>011944_02.ma2</path></file><level>11</level><levelDecimal>5</levelDecimal><notesDesigner><id>63</id><str>アマリリス</str></notesDesigner><notesType>0</notesType><musicLevelID>15</musicLevelID><maxNotes>535</maxNotes><isEnable>true</isEnable></Notes>\n"
      "    <Notes><file><path>011944_03.ma2</path></file><level>13</level><levelDecimal>8</levelDecimal><notesDesigner><id>128</id><str>Luxizhel</str></notesDesigner><notesType>0</notesType><musicLevelID>20</musicLevelID><maxNotes>920</maxNotes><isEnable>true</isEnable></Notes>\n"
      "    <Notes><file><path>011944_04.ma2</path></file><level>0</level><levelDecimal>0</levelDecimal><notesDesigner><id>0</id><str /></notesDesigner><notesType>0</notesType><musicLevelID>0</musicLevelID><maxNotes>0</maxNotes><isEnable>false</isEnable></Notes>\n"
      "    <Notes><file><path>011944_05.ma2</path></file><level>0</level><levelDecimal>0</levelDecimal><notesDesigner><id>0</id><str /></notesDesigner><notesType>0</notesType><musicLevelID>0</musicLevelID><maxNotes>0</maxNotes><isEnable>false</isEnable></Notes>\n"
      "  </notesData>\n"
      "  <utageKanjiName />\n"
      "  <comment />\n"
      "  <utagePlayStyle>0</utagePlayStyle>\n"
      "</MusicData>\n";
    write_text_file(track_folder / "Music.xml", xml);

    fs::create_directories(db_root / "SoundData");
    fs::create_directories(db_root / "AssetBundleImages" / "jacket");
    fs::create_directories(db_root / "MovieData");
    write_text_file(db_root / "SoundData" / "music001944.mp3", "dummy");
    write_text_file(db_root / "AssetBundleImages" / "jacket" / "UI_Jacket_001944.png", "dummy");
    write_text_file(db_root / "MovieData" / "001944.mp4", "dummy");
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

TEST_CASE("assets reports per-track progress before final total by default") {
  const fs::path temp_root = unique_temp_dir("assets_progress_default");
  const fs::path assets_root = temp_root / "StreamingAssets";
  const fs::path output_root = temp_root / "output";

  fs::create_directories(assets_root);
  create_track(assets_root / "A000", "000123", "ProgressSong", "POPS", "PRISM");

  AssetsOptions options;
  options.streaming_assets_path = assets_root;
  options.output_path = output_root;
  options.format = ChartFormat::Simai;

  std::ostringstream captured_out;
  std::ostringstream captured_err;
  auto* old_out = std::cout.rdbuf(captured_out.rdbuf());
  auto* old_err = std::cerr.rdbuf(captured_err.rdbuf());
  const int result = run_compile_assets(options);
  std::cout.rdbuf(old_out);
  std::cerr.rdbuf(old_err);

  REQUIRE(result == 0);
  const std::string out = captured_out.str();
  REQUIRE(out.find("Completed: 000123 ProgressSong") != std::string::npos);
  REQUIRE(out.find("Total music compiled: 1") != std::string::npos);
  REQUIRE(out.find("Completed: 000123 ProgressSong") < out.find("Total music compiled: 1"));
  REQUIRE(captured_err.str().empty());

  fs::remove_all(temp_root);
}

TEST_CASE("assets quiet log level suppresses per-track output") {
  const fs::path temp_root = unique_temp_dir("assets_progress_quiet");
  const fs::path assets_root = temp_root / "StreamingAssets";
  const fs::path output_root = temp_root / "output";

  fs::create_directories(assets_root);
  create_track(assets_root / "A000", "000124", "QuietSong", "POPS", "PRISM");

  AssetsOptions options;
  options.streaming_assets_path = assets_root;
  options.output_path = output_root;
  options.format = ChartFormat::Simai;
  options.log_level = AssetsLogLevel::Quiet;

  std::ostringstream captured_out;
  std::ostringstream captured_err;
  auto* old_out = std::cout.rdbuf(captured_out.rdbuf());
  auto* old_err = std::cerr.rdbuf(captured_err.rdbuf());
  const int result = run_compile_assets(options);
  std::cout.rdbuf(old_out);
  std::cerr.rdbuf(old_err);

  REQUIRE(result == 0);
  const std::string out = captured_out.str();
  REQUIRE(out.find("Completed:") == std::string::npos);
  REQUIRE(out.find("Incomplete:") == std::string::npos);
  REQUIRE(out.find("Total music compiled: 1") != std::string::npos);
  REQUIRE(captured_err.str().empty());

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

  std::ostringstream captured_out;
  std::ostringstream captured_err;
  auto* old_out = std::cout.rdbuf(captured_out.rdbuf());
  auto* old_err = std::cerr.rdbuf(captured_err.rdbuf());
  const int result = run_compile_assets(options);
  std::cout.rdbuf(old_out);
  std::cerr.rdbuf(old_err);

  REQUIRE(result == 0);

  const fs::path incomplete_dir = output_root / "011944_RawMediaSong [DX]_Incomplete";
  REQUIRE(fs::exists(incomplete_dir / "maidata.txt"));
  REQUIRE_FALSE(fs::exists(incomplete_dir / "track.acb"));
  REQUIRE_FALSE(fs::exists(incomplete_dir / "track.awb"));
  REQUIRE_FALSE(fs::exists(incomplete_dir / "bg.ab"));
  REQUIRE_FALSE(fs::exists(incomplete_dir / "pv.dat"));

  REQUIRE_FALSE(fs::exists(output_root / "_log.txt"));
  const std::string out = captured_out.str();
  REQUIRE(out.find("Incomplete: 011944 RawMediaSong [DX]") != std::string::npos);
  REQUIRE(out.find("Total music compiled: 0") != std::string::npos);
  REQUIRE(out.find("Incomplete: 011944 RawMediaSong [DX]") < out.find("Total music compiled: 0"));
  REQUIRE(captured_err.str().find("Audio conversion failed") != std::string::npos);
  REQUIRE(captured_err.str().find("Cover conversion failed") != std::string::npos);
  REQUIRE(captured_err.str().find("Video conversion failed") != std::string::npos);

  fs::remove_all(temp_root);
}

TEST_CASE("assets verbose log level prints paths and immediate warnings") {
  const fs::path temp_root = unique_temp_dir("assets_progress_verbose");
  const fs::path assets_root = temp_root / "StreamingAssets";
  const fs::path output_root = temp_root / "output";

  fs::create_directories(assets_root);
  create_track(assets_root / "A045", "011944", "VerboseSong", "GAME", "PRISM");
  create_compact_media_assets(assets_root / "A045", "011944");

  AssetsOptions options;
  options.streaming_assets_path = assets_root;
  options.output_path = output_root;
  options.format = ChartFormat::Simai;
  options.ignore_incomplete_assets = true;
  options.log_level = AssetsLogLevel::Verbose;

  std::ostringstream captured_out;
  std::ostringstream captured_err;
  auto* old_out = std::cout.rdbuf(captured_out.rdbuf());
  auto* old_err = std::cerr.rdbuf(captured_err.rdbuf());
  const int result = run_compile_assets(options);
  std::cout.rdbuf(old_out);
  std::cerr.rdbuf(old_err);

  REQUIRE(result == 0);
  const std::string out = captured_out.str();
  REQUIRE(out.find("Incomplete: 011944 VerboseSong [DX] -> ") != std::string::npos);
  REQUIRE(out.find("Total music compiled: 0") != std::string::npos);
  REQUIRE(captured_err.str().find("Warning: Audio conversion failed") != std::string::npos);
  REQUIRE(captured_err.str().find("Warning: Cover conversion failed") != std::string::npos);
  REQUIRE(captured_err.str().find("Warning: Video conversion failed") != std::string::npos);
  REQUIRE(captured_err.str().find("Warnings:\n") == std::string::npos);

  fs::remove_all(temp_root);
}

TEST_CASE("assets supports pseudo assetbundle jacket in .ab") {
  const fs::path temp_root = unique_temp_dir("assets_pseudo_ab_jacket");
  const fs::path assets_root = temp_root / "StreamingAssets";
  const fs::path output_root = temp_root / "output";

  fs::create_directories(assets_root);
  create_track(assets_root / "A045", "011944", "PseudoABJacketSong", "GAME", "PRISM");

  const fs::path db_root = assets_root / "A045";
  fs::create_directories(db_root / "SoundData");
  fs::create_directories(db_root / "AssetBundleImages" / "jacket");
  fs::create_directories(db_root / "MovieData");

  write_text_file(db_root / "SoundData" / "music001944.mp3", "dummy");
  write_text_file(db_root / "MovieData" / "001944.mp4", "dummy");

  // JPEG SOI marker (FF D8 FF) is enough for pseudo-image detection path.
  write_binary_file(db_root / "AssetBundleImages" / "jacket" / "ui_jacket_001944.ab",
    { 0xFFU, 0xD8U, 0xFFU, 0xE0U, 0x00U, 0x10U, 0xFFU, 0xD9U });

  AssetsOptions options;
  options.streaming_assets_path = assets_root;
  options.output_path = output_root;
  options.format = ChartFormat::Simai;

  REQUIRE(run_compile_assets(options) == 0);
  REQUIRE(fs::exists(output_root / "011944_PseudoABJacketSong [DX]" / "maidata.txt"));
  REQUIRE(fs::exists(output_root / "011944_PseudoABJacketSong [DX]" / "track.mp3"));
  REQUIRE(fs::exists(output_root / "011944_PseudoABJacketSong [DX]" / "pv.mp4"));
  REQUIRE(fs::exists(output_root / "011944_PseudoABJacketSong [DX]" / "bg.jpg"));
  REQUIRE_FALSE(fs::exists(output_root / "011944_PseudoABJacketSong [DX]_Incomplete"));

  fs::remove_all(temp_root);
}
TEST_CASE("assets exports selected id with all difficulties when difficulty is omitted") {
  const fs::path temp_root = unique_temp_dir("assets_id_all_diff");
  const fs::path assets_root = temp_root / "StreamingAssets";
  const fs::path output_root = temp_root / "output";

  fs::create_directories(assets_root);
  create_track(assets_root / "A000", "000777", "SelectMe", "POPS", "PRISM", { 2, 3 });
  create_track(assets_root / "A000", "000888", "IgnoreMe", "POPS", "PRISM", { 3 });

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
  REQUIRE(fs::exists(output_root / "011944_Restricted Access [DX]" / "maidata.txt"));
  REQUIRE_FALSE(fs::exists(output_root / "260116_Restricted Access"));

  fs::remove_all(temp_root);
}

TEST_CASE("assets exports maidata format with metadata fields", "[assets][maidata]") {
  const fs::path temp_root = unique_temp_dir("assets_maidata_format");
  const fs::path assets_root = temp_root / "StreamingAssets";
  const fs::path output_root = temp_root / "output";

  const fs::path track_folder = assets_root / "A045" / "music" / "music011944";
  fs::create_directories(track_folder);
  write_text_file(track_folder / "011944_00.ma2", sample_ma2());
  write_text_file(track_folder / "011944_01.ma2", sample_ma2());

  const std::string xml =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
    "<MusicData>\n"
    "  <name><id>11944</id><str>Restricted Access</str></name>\n"
    "  <artistName><id>1</id><str>Knighthood</str></artistName>\n"
    "  <genreName><id>104</id><str>ゲームバラエティ</str></genreName>\n"
    "  <bpm>185</bpm>\n"
    "  <version>25007</version>\n"
    "  <AddVersion><id>23</id><str>PRiSM</str></AddVersion>\n"
    "  <notesData>\n"
    "    <Notes><file><path>011944_00.ma2</path></file><level>4</level><levelDecimal>0</levelDecimal><musicLevelID>4</musicLevelID><notesDesigner><str></str></notesDesigner><isEnable>true</isEnable></Notes>\n"
    "    <Notes><file><path>011944_01.ma2</path></file><level>7</level><levelDecimal>8</levelDecimal><musicLevelID>8</musicLevelID><notesDesigner><str>Tester</str></notesDesigner><isEnable>true</isEnable></Notes>\n"
    "  </notesData>\n"
    "</MusicData>\n";
  write_text_file(track_folder / "Music.xml", xml);

  AssetsOptions options;
  options.streaming_assets_path = assets_root;
  options.output_path = output_root;
  options.format = ChartFormat::Maidata;

  REQUIRE(run_compile_assets(options) == 0);
  const std::string maidata = read_text_file(output_root / "011944_Restricted Access [DX]" / "maidata.txt");
  REQUIRE(maidata.find("&shortid=11944") != std::string::npos);
  REQUIRE(maidata.find("&genreid=104") != std::string::npos);
  REQUIRE(maidata.find("&versionid=23") != std::string::npos);
  REQUIRE(maidata.find("&version=PRiSM") != std::string::npos);
  REQUIRE(maidata.find("&lv_2=4.0") != std::string::npos);
  REQUIRE(maidata.find("&lv_3=7.8") != std::string::npos);
  REQUIRE(maidata.find("&inote_2=") != std::string::npos);
  REQUIRE(maidata.find("&inote_3=") != std::string::npos);

  fs::remove_all(temp_root);
}

TEST_CASE("assets maidata normalizes genre labels and preserves special title characters", "[assets][maidata]") {
  const fs::path temp_root = unique_temp_dir("assets_maidata_genre_mapping");
  const fs::path assets_root = temp_root / "StreamingAssets";
  const fs::path output_root = temp_root / "output";

  const fs::path track_folder = assets_root / "A045" / "music" / "music011944";
  fs::create_directories(track_folder);
  write_text_file(track_folder / "011944_00.ma2", sample_ma2());

  const std::string xml =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
    "<MusicData>\n"
    "  <name><id>11944</id><str>Rock &amp; Roll?</str></name>\n"
    "  <artistName><id>1</id><str>Knighthood</str></artistName>\n"
    "  <genreName><id>104</id><str>ゲームバラエティ</str></genreName>\n"
    "  <bpm>185</bpm>\n"
    "  <version>25007</version>\n"
    "  <AddVersion><id>23</id><str>PRiSM</str></AddVersion>\n"
    "  <notesData>\n"
    "    <Notes><file><path>011944_00.ma2</path></file><level>4</level><levelDecimal>0</levelDecimal><musicLevelID>4</musicLevelID><notesDesigner><str></str></notesDesigner><isEnable>true</isEnable></Notes>\n"
    "  </notesData>\n"
    "</MusicData>\n";
  write_text_file(track_folder / "Music.xml", xml);

  AssetsOptions options;
  options.streaming_assets_path = assets_root;
  options.output_path = output_root;
  options.format = ChartFormat::Maidata;

  REQUIRE(run_compile_assets(options) == 0);
  const std::string maidata = read_text_file(output_root / "011944_Rock & Roll_ [DX]" / "maidata.txt");
  REQUIRE(maidata.find("&title=Rock & Roll? [DX]") != std::string::npos);
  REQUIRE(maidata.find("&genre=ゲーム&バラエティ") != std::string::npos);

  SimaiTokenizer tokenizer;
  const auto doc = tokenizer.parse_document(maidata);
  REQUIRE(doc.metadata.at("title") == "Rock & Roll? [DX]");
  REQUIRE(doc.metadata.at("genre") == "ゲーム&バラエティ");

  fs::remove_all(temp_root);
}

TEST_CASE("assets maidata can export display levels in lv fields", "[assets][maidata]") {
  const fs::path temp_root = unique_temp_dir("assets_maidata_display_levels");
  const fs::path assets_root = temp_root / "StreamingAssets";
  const fs::path output_root = temp_root / "output";

  const fs::path track_folder = assets_root / "A045" / "music" / "music011944";
  fs::create_directories(track_folder);
  write_text_file(track_folder / "011944_00.ma2", sample_ma2());
  write_text_file(track_folder / "011944_01.ma2", sample_ma2());
  write_text_file(track_folder / "011944_02.ma2", sample_ma2());
  write_text_file(track_folder / "011944_03.ma2", sample_ma2());

  const std::string xml =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
    "<MusicData>\n"
    "  <name><id>11944</id><str>Restricted Access</str></name>\n"
    "  <artistName><id>1</id><str>Knighthood</str></artistName>\n"
    "  <genreName><id>104</id><str>ゲームバラエティ</str></genreName>\n"
    "  <bpm>185</bpm>\n"
    "  <version>25007</version>\n"
    "  <AddVersion><id>23</id><str>PRiSM</str></AddVersion>\n"
    "  <notesData>\n"
    "    <Notes><file><path>011944_00.ma2</path></file><level>4</level><levelDecimal>0</levelDecimal><musicLevelID>4</musicLevelID><notesDesigner><str></str></notesDesigner><isEnable>true</isEnable></Notes>\n"
    "    <Notes><file><path>011944_01.ma2</path></file><level>7</level><levelDecimal>8</levelDecimal><musicLevelID>8</musicLevelID><notesDesigner><str>Tester</str></notesDesigner><isEnable>true</isEnable></Notes>\n"
    "    <Notes><file><path>011944_02.ma2</path></file><level>11</level><levelDecimal>5</levelDecimal><musicLevelID>15</musicLevelID><notesDesigner><str>Tester</str></notesDesigner><isEnable>true</isEnable></Notes>\n"
    "    <Notes><file><path>011944_03.ma2</path></file><level>13</level><levelDecimal>8</levelDecimal><musicLevelID>20</musicLevelID><notesDesigner><str>Tester</str></notesDesigner><isEnable>true</isEnable></Notes>\n"
    "  </notesData>\n"
    "</MusicData>\n";
  write_text_file(track_folder / "Music.xml", xml);

  AssetsOptions options;
  options.streaming_assets_path = assets_root;
  options.output_path = output_root;
  options.format = ChartFormat::Maidata;
  options.maidata_level_mode = MaidataLevelMode::Display;

  REQUIRE(run_compile_assets(options) == 0);
  const std::string maidata = read_text_file(output_root / "011944_Restricted Access [DX]" / "maidata.txt");
  REQUIRE(maidata.find("&lv_2=4") != std::string::npos);
  REQUIRE(maidata.find("&lv_3=7+") != std::string::npos);
  REQUIRE(maidata.find("&lv_4=11") != std::string::npos);
  REQUIRE(maidata.find("&lv_5=13+") != std::string::npos);

  fs::remove_all(temp_root);
}

TEST_CASE("assets maidata falls back to filename difficulties without notesData", "[assets][maidata]") {
  const fs::path temp_root = unique_temp_dir("assets_maidata_one_based_diff");
  const fs::path assets_root = temp_root / "StreamingAssets";
  const fs::path output_root = temp_root / "output";

  fs::create_directories(assets_root);
  create_track(assets_root / "A045", "019999", "OneBasedSong", "GAME", "PRISM", { 1, 7 });

  AssetsOptions options;
  options.streaming_assets_path = assets_root;
  options.output_path = output_root;
  options.format = ChartFormat::Maidata;

  REQUIRE(run_compile_assets(options) == 0);
  const std::string maidata = read_text_file(output_root / "019999_OneBasedSong [DX]" / "maidata.txt");
  REQUIRE(maidata.find("&inote_1=") != std::string::npos);
  REQUIRE(maidata.find("&inote_7=") != std::string::npos);

  fs::remove_all(temp_root);
}

TEST_CASE("assets selected difficulty follows notesData maidata numbering", "[assets][maidata]") {
  const fs::path temp_root = unique_temp_dir("assets_notesdata_selected_diff");
  const fs::path assets_root = temp_root / "StreamingAssets";
  const fs::path output_root = temp_root / "output";

  const fs::path track_folder = assets_root / "A045" / "music" / "music011944";
  fs::create_directories(track_folder);
  write_text_file(track_folder / "011944_00.ma2", sample_ma2());
  write_text_file(track_folder / "011944_01.ma2", sample_ma2());

  const std::string xml =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
    "<MusicData>\n"
    "  <name><id>11944</id><str>Restricted Access</str></name>\n"
    "  <artistName><id>1</id><str>Knighthood</str></artistName>\n"
    "  <genreName><id>104</id><str>ゲームバラエティ</str></genreName>\n"
    "  <bpm>185</bpm>\n"
    "  <version>25007</version>\n"
    "  <AddVersion><id>23</id><str>PRiSM</str></AddVersion>\n"
    "  <notesData>\n"
    "    <Notes><file><path>011944_00.ma2</path></file><level>4</level><levelDecimal>0</levelDecimal><musicLevelID>4</musicLevelID><notesDesigner><str></str></notesDesigner><isEnable>true</isEnable></Notes>\n"
    "    <Notes><file><path>011944_01.ma2</path></file><level>7</level><levelDecimal>8</levelDecimal><musicLevelID>8</musicLevelID><notesDesigner><str>Tester</str></notesDesigner><isEnable>true</isEnable></Notes>\n"
    "  </notesData>\n"
    "</MusicData>\n";
  write_text_file(track_folder / "Music.xml", xml);

  AssetsOptions options;
  options.streaming_assets_path = assets_root;
  options.output_path = output_root;
  options.format = ChartFormat::Maidata;
  options.target_music_id = "11944";
  options.target_difficulty = 2;

  REQUIRE(run_compile_assets(options) == 0);
  const std::string maidata = read_text_file(output_root / "011944_Restricted Access [DX]" / "maidata.txt");
  REQUIRE(maidata.find("&lv_2=4.0") != std::string::npos);
  REQUIRE(maidata.find("&inote_2=") != std::string::npos);
  REQUIRE(maidata.find("&lv_3=7.8") == std::string::npos);
  REQUIRE(maidata.find("&inote_3=") == std::string::npos);

  fs::remove_all(temp_root);
}

TEST_CASE("assets maidata exports utage charts to slot 7", "[assets][maidata]") {
  const fs::path temp_root = unique_temp_dir("assets_utage_slot7");
  const fs::path assets_root = temp_root / "StreamingAssets";
  const fs::path output_root = temp_root / "output";

  const fs::path track_folder = assets_root / "A045" / "music" / "music101234";
  fs::create_directories(track_folder);
  write_text_file(track_folder / "101234_00.ma2", sample_ma2());

  const std::string xml =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
    "<MusicData xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\">\n"
    "  <dataName>music101234</dataName>\n"
    "  <netOpenName><id>260999</id><str>Net260999</str></netOpenName>\n"
    "  <releaseTagName><id>5001</id><str>Ver1.50.00</str></releaseTagName>\n"
    "  <disable>false</disable>\n"
    "  <name><id>101234</id><str>[宴]Test Utage</str></name>\n"
    "  <sortName>[宴]TESTUTAGE</sortName>\n"
    "  <artistName><id>1</id><str>Tester</str></artistName>\n"
    "  <genreName><id>104</id><str>ゲームバラエティ</str></genreName>\n"
    "  <bpm>180</bpm>\n"
    "  <version>25007</version>\n"
    "  <AddVersion><id>23</id><str>PRiSM</str></AddVersion>\n"
    "  <notesData>\n"
    "    <Notes>\n"
    "      <file><path>101234_00.ma2</path></file>\n"
    "      <level>14</level>\n"
    "      <levelDecimal>1</levelDecimal>\n"
    "      <notesDesigner><id>128</id><str>宴テスター</str></notesDesigner>\n"
    "      <notesType>0</notesType>\n"
    "      <musicLevelID>22</musicLevelID>\n"
    "      <maxNotes>999</maxNotes>\n"
    "      <isEnable>true</isEnable>\n"
    "    </Notes>\n"
    "  </notesData>\n"
    "  <utageKanjiName><id>0</id><str>宴試験</str></utageKanjiName>\n"
    "  <comment />\n"
    "  <utagePlayStyle>1</utagePlayStyle>\n"
    "</MusicData>\n";
  write_text_file(track_folder / "Music.xml", xml);

  AssetsOptions options;
  options.streaming_assets_path = assets_root;
  options.output_path = output_root;
  options.format = ChartFormat::Maidata;

  REQUIRE(run_compile_assets(options) == 0);
  const fs::path maidata_path = append_utf8_path(output_root, "101234_" + sanitize_folder_name("[宴]Test Utage")) / "maidata.txt";
  const std::string maidata = read_text_file(maidata_path);
  REQUIRE(maidata.find("&lv_7=14.1") != std::string::npos);
  REQUIRE(maidata.find("&des_7=宴テスター") != std::string::npos);
  REQUIRE(maidata.find("&inote_7=") != std::string::npos);
  REQUIRE(maidata.find("&lv_2=") == std::string::npos);
  REQUIRE(maidata.find("&inote_2=") == std::string::npos);

  fs::remove_all(temp_root);
}

TEST_CASE("assets selected difficulty 7 exports utage chart only", "[assets][maidata]") {
  const fs::path temp_root = unique_temp_dir("assets_utage_selected_diff");
  const fs::path assets_root = temp_root / "StreamingAssets";
  const fs::path output_root = temp_root / "output";

  const fs::path track_folder = assets_root / "A045" / "music" / "music101235";
  fs::create_directories(track_folder);
  write_text_file(track_folder / "101235_00.ma2", sample_ma2());

  const std::string xml =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
    "<MusicData>\n"
    "  <name><id>101235</id><str>[宴]Filter Utage</str></name>\n"
    "  <artistName><id>1</id><str>Tester</str></artistName>\n"
    "  <genreName><id>104</id><str>ゲームバラエティ</str></genreName>\n"
    "  <bpm>181</bpm>\n"
    "  <version>25007</version>\n"
    "  <AddVersion><id>23</id><str>PRiSM</str></AddVersion>\n"
    "  <notesData>\n"
    "    <Notes><file><path>101235_00.ma2</path></file><level>14</level><levelDecimal>3</levelDecimal><musicLevelID>22</musicLevelID><notesDesigner><str>宴テスター</str></notesDesigner><isEnable>true</isEnable></Notes>\n"
    "  </notesData>\n"
    "  <utageKanjiName><id>0</id><str>宴試験</str></utageKanjiName>\n"
    "  <utagePlayStyle>1</utagePlayStyle>\n"
    "</MusicData>\n";
  write_text_file(track_folder / "Music.xml", xml);

  AssetsOptions options;
  options.streaming_assets_path = assets_root;
  options.output_path = output_root;
  options.format = ChartFormat::Maidata;
  options.target_music_id = "101235";
  options.target_difficulty = 7;

  REQUIRE(run_compile_assets(options) == 0);
  const fs::path maidata_path = append_utf8_path(output_root, "101235_" + sanitize_folder_name("[宴]Filter Utage")) / "maidata.txt";
  const std::string maidata = read_text_file(maidata_path);
  REQUIRE(maidata.find("&lv_7=14.3") != std::string::npos);
  REQUIRE(maidata.find("&inote_7=") != std::string::npos);
  REQUIRE(maidata.find("&lv_2=") == std::string::npos);
  REQUIRE(maidata.find("&inote_2=") == std::string::npos);

  fs::remove_all(temp_root);
}

TEST_CASE("assets maidata display mode keeps utage chart on slot 7", "[assets][maidata]") {
  const fs::path temp_root = unique_temp_dir("assets_utage_display_slot7");
  const fs::path assets_root = temp_root / "StreamingAssets";
  const fs::path output_root = temp_root / "output";

  const fs::path track_folder = assets_root / "A045" / "music" / "music101236";
  fs::create_directories(track_folder);
  write_text_file(track_folder / "101236_00.ma2", sample_ma2());

  const std::string xml =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
    "<MusicData>\n"
    "  <name><id>101236</id><str>[宴]Display Utage</str></name>\n"
    "  <artistName><id>1</id><str>Tester</str></artistName>\n"
    "  <genreName><id>104</id><str>ゲームバラエティ</str></genreName>\n"
    "  <bpm>182</bpm>\n"
    "  <version>25007</version>\n"
    "  <AddVersion><id>23</id><str>PRiSM</str></AddVersion>\n"
    "  <notesData>\n"
    "    <Notes><file><path>101236_00.ma2</path></file><level>14</level><levelDecimal>6</levelDecimal><musicLevelID>22</musicLevelID><notesDesigner><str>宴テスター</str></notesDesigner><isEnable>true</isEnable></Notes>\n"
    "  </notesData>\n"
    "  <utageKanjiName><id>0</id><str>宴試験</str></utageKanjiName>\n"
    "  <utagePlayStyle>1</utagePlayStyle>\n"
    "</MusicData>\n";
  write_text_file(track_folder / "Music.xml", xml);

  AssetsOptions options;
  options.streaming_assets_path = assets_root;
  options.output_path = output_root;
  options.format = ChartFormat::Maidata;
  options.maidata_level_mode = MaidataLevelMode::Display;

  REQUIRE(run_compile_assets(options) == 0);
  const fs::path maidata_path = append_utf8_path(output_root, "101236_" + sanitize_folder_name("[宴]Display Utage")) / "maidata.txt";
  const std::string maidata = read_text_file(maidata_path);
  REQUIRE(maidata.find("&lv_7=14+") != std::string::npos);
  REQUIRE(maidata.find("&des_7=宴テスター") != std::string::npos);
  REQUIRE(maidata.find("&inote_7=") != std::string::npos);
  REQUIRE(maidata.find("&lv_2=") == std::string::npos);

  fs::remove_all(temp_root);
}

TEST_CASE("assets can export self-contained streamingassets fixture with maidata numbering", "[assets][maidata]") {
  const fs::path temp_root = unique_temp_dir("assets_self_contained_fixture_export");
  const fs::path assets_root = temp_root / "StreamingAssets";
  const fs::path output_root = temp_root / "output";

  create_complete_maidata_fixture_011944(assets_root);

  AssetsOptions options;
  options.streaming_assets_path = assets_root;
  options.output_path = output_root;
  options.format = ChartFormat::Maidata;
  options.target_music_id = "11944";

  REQUIRE(run_compile_assets(options) == 0);

  const fs::path maidata_path = append_utf8_path(output_root, "011944_Restricted Access [DX]") / "maidata.txt";
  REQUIRE(fs::exists(maidata_path));

  const std::string maidata = read_text_file(maidata_path);
  REQUIRE(maidata.find("&title=Restricted Access [DX]") != std::string::npos);
  REQUIRE(maidata.find("&shortid=11944") != std::string::npos);
  REQUIRE(maidata.find("&lv_2=4.0") != std::string::npos);
  REQUIRE(maidata.find("&lv_3=7.8") != std::string::npos);
  REQUIRE(maidata.find("&lv_4=11.5") != std::string::npos);
  REQUIRE(maidata.find("&lv_5=13.8") != std::string::npos);
  REQUIRE(maidata.find("&inote_2=") != std::string::npos);
  REQUIRE(maidata.find("&inote_5=") != std::string::npos);
  REQUIRE(fs::exists(append_utf8_path(output_root, "011944_Restricted Access [DX]") / "track.mp3"));
  REQUIRE(fs::exists(append_utf8_path(output_root, "011944_Restricted Access [DX]") / "bg.png"));
  REQUIRE(fs::exists(append_utf8_path(output_root, "011944_Restricted Access [DX]") / "pv.mp4"));

  fs::remove_all(temp_root);
}

TEST_CASE("assets maidata uses canonical inote formatting for real fixture", "[assets][maidata]") {
  const fs::path assets_root = repo_test_data_path("StreamingAssets");
  const fs::path sample = assets_root / "A045" / "music" / "music011944" / "011944_00.ma2";
  if (!fs::exists(sample)) {
    SKIP("real StreamingAssets maidata fixture not found in test workspace");
  }

  const fs::path temp_root = unique_temp_dir("assets_real_fixture_inote_style");
  const fs::path output_root = temp_root / "output";

  AssetsOptions options;
  options.streaming_assets_path = assets_root;
  options.output_path = output_root;
  options.format = ChartFormat::Maidata;
  options.target_music_id = "11944";

  REQUIRE(run_compile_assets(options) == 0);

  const fs::path maidata_path = append_utf8_path(output_root, "011944_Restricted Access [DX]") / "maidata.txt";
  REQUIRE(fs::exists(maidata_path));

  const std::string maidata = read_text_file(maidata_path);
  REQUIRE(maidata.find("&inote_2=(185){1},\n{1},\n{1}1x,\n") != std::string::npos);
  REQUIRE(maidata.find("{1}1-5[4:3],") != std::string::npos);
  REQUIRE(maidata.find("##") == std::string::npos);
  REQUIRE(maidata.find("/{4}/") == std::string::npos);

  fs::remove_all(temp_root);
}

TEST_CASE("assets can export self-contained streamingassets fixture with display levels", "[assets][maidata]") {
  const fs::path temp_root = unique_temp_dir("assets_self_contained_fixture_display_export");
  const fs::path assets_root = temp_root / "StreamingAssets";
  const fs::path output_root = temp_root / "output";

  create_complete_maidata_fixture_011944(assets_root);

  AssetsOptions options;
  options.streaming_assets_path = assets_root;
  options.output_path = output_root;
  options.format = ChartFormat::Maidata;
  options.target_music_id = "11944";
  options.maidata_level_mode = MaidataLevelMode::Display;

  REQUIRE(run_compile_assets(options) == 0);

  const fs::path maidata_path = append_utf8_path(output_root, "011944_Restricted Access [DX]") / "maidata.txt";
  REQUIRE(fs::exists(maidata_path));

  const std::string maidata = read_text_file(maidata_path);
  REQUIRE(maidata.find("&title=Restricted Access [DX]") != std::string::npos);
  REQUIRE(maidata.find("&shortid=11944") != std::string::npos);
  REQUIRE(maidata.find("&lv_2=4") != std::string::npos);
  REQUIRE(maidata.find("&lv_3=7+") != std::string::npos);
  REQUIRE(maidata.find("&lv_4=11") != std::string::npos);
  REQUIRE(maidata.find("&lv_5=13+") != std::string::npos);
  REQUIRE(maidata.find("&inote_2=") != std::string::npos);
  REQUIRE(maidata.find("&inote_5=") != std::string::npos);
  REQUIRE(fs::exists(append_utf8_path(output_root, "011944_Restricted Access [DX]") / "track.mp3"));
  REQUIRE(fs::exists(append_utf8_path(output_root, "011944_Restricted Access [DX]") / "bg.png"));
  REQUIRE(fs::exists(append_utf8_path(output_root, "011944_Restricted Access [DX]") / "pv.mp4"));

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
  create_track(assets_root / "A000", "000999", "DiffSong", "POPS", "PRISM", { 2, 3 });

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


