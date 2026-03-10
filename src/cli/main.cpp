#include "maiconv/core/assets.hpp"
#include "maiconv/core/io.hpp"
#include "maiconv/core/ma2.hpp"
#include "maiconv/core/media.hpp"
#include "maiconv/core/simai.hpp"

#include <CLI/CLI.hpp>

#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>

namespace {

  constexpr int kSuccess = 0;
  constexpr int kFailure = 2;

  std::filesystem::path resolve_output_path(const std::string& output, const std::string& default_file_name) {
    std::filesystem::path out(output);
    if (out.extension().empty()) {
      return out / default_file_name;
    }
    return out;
  }

  std::filesystem::path resolve_binary_output_path(const std::string& output, const std::string& default_file_name) {
    if (output.empty()) {
      return std::filesystem::current_path() / default_file_name;
    }
    return resolve_output_path(output, default_file_name);
  }

  void write_or_stdout(const std::string& output, const std::string& default_file_name, const std::string& payload) {
    if (output.empty()) {
      std::cout << payload;
      if (payload.empty() || payload.back() != '\n') {
        std::cout << "\n";
      }
      return;
    }
    const auto target = resolve_output_path(output, default_file_name);
    maiconv::write_text_file(target, payload);
    std::cout << "Successfully compiled at: " << target.string() << "\n";
  }

  int run_ma2_to(const std::filesystem::path& input,
    maiconv::ChartFormat format,
    const std::optional<maiconv::FlipMethod>& rotate,
    int shift_ticks,
    const std::string& output) {
    try {
      maiconv::Ma2Tokenizer tokenizer;
      maiconv::Ma2Parser parser;
      maiconv::Ma2Composer ma2_composer;
      maiconv::SimaiComposer simai_composer;

      maiconv::Chart chart = parser.parse(tokenizer.tokenize_file(input));
      if (rotate.has_value()) {
        chart.rotate(*rotate);
      }
      if (shift_ticks != 0) {
        chart.shift_by_offset(shift_ticks);
      }

      std::string result;
      std::string file_name;
      if (format == maiconv::ChartFormat::Simai || format == maiconv::ChartFormat::SimaiFes) {
        result = simai_composer.compose_chart(chart);
        file_name = "maidata.txt";
      }
      else if (format == maiconv::ChartFormat::Maidata) {
        result = "&inote_1=\n" + simai_composer.compose_chart(chart) + "\n";
        file_name = "maidata.txt";
      }
      else {
        result = ma2_composer.compose(chart, format);
        file_name = "result.ma2";
      }

      write_or_stdout(output, file_name, result);
      return kSuccess;
    }
    catch (const std::exception& ex) {
      std::cerr << "Program cannot proceed because of following error returned:\n" << ex.what() << "\n";
      return kFailure;
    }
  }

  int run_simai_to(const std::filesystem::path& input,
    std::optional<int> difficulty,
    maiconv::ChartFormat format,
    const std::optional<maiconv::FlipMethod>& rotate,
    int shift_ticks,
    const std::string& output) {
    try {
      maiconv::SimaiTokenizer tokenizer;
      maiconv::SimaiParser parser;
      maiconv::Ma2Composer ma2_composer;
      maiconv::SimaiComposer simai_composer;

      const auto doc = tokenizer.parse_file(input);
      const int diff = difficulty.value_or(1);
      maiconv::Chart chart = parser.parse_document(doc, diff);

      if (rotate.has_value()) {
        chart.rotate(*rotate);
      }
      if (shift_ticks != 0) {
        chart.shift_by_offset(shift_ticks);
      }

      std::string result;
      std::string file_name;
      if (format == maiconv::ChartFormat::Simai || format == maiconv::ChartFormat::SimaiFes) {
        result = simai_composer.compose_chart(chart);
        file_name = "maidata.txt";
      }
      else if (format == maiconv::ChartFormat::Maidata) {
        result = "&inote_" + std::to_string(diff) + "=\n" + simai_composer.compose_chart(chart) + "\n";
        file_name = "maidata.txt";
      }
      else {
        result = ma2_composer.compose(chart, format);
        file_name = "result.ma2";
      }

      write_or_stdout(output, file_name, result);
      return kSuccess;
    }
    catch (const std::exception& ex) {
      std::cerr << "Program cannot proceed because of following error returned:\n" << ex.what() << "\n";
      return kFailure;
    }
  }

  int run_media_audio_to_mp3(const std::filesystem::path& acb,
    const std::filesystem::path& awb,
    const std::string& output) {
    try {
      const auto target = resolve_binary_output_path(output, "track.mp3");
      if (!maiconv::convert_acb_awb_to_mp3(acb, awb, target)) {
        throw std::runtime_error("Audio conversion failed: " + acb.string() + " + " + awb.string() + " -> " +
          target.string());
      }
      std::cout << "Successfully converted at: " << target.string() << "\n";
      return kSuccess;
    }
    catch (const std::exception& ex) {
      std::cerr << "Program cannot proceed because of following error returned:\n" << ex.what() << "\n";
      return kFailure;
    }
  }

  int run_media_cover_to_png(const std::filesystem::path& input_ab, const std::string& output) {
    try {
      const auto target = resolve_binary_output_path(output, "bg.png");
      if (!maiconv::convert_ab_to_png(input_ab, target)) {
        throw std::runtime_error("Cover conversion failed: " + input_ab.string() + " -> " + target.string());
      }
      std::cout << "Successfully converted at: " << target.string() << "\n";
      return kSuccess;
    }
    catch (const std::exception& ex) {
      std::cerr << "Program cannot proceed because of following error returned:\n" << ex.what() << "\n";
      return kFailure;
    }
  }

  int run_media_video_to_mp4(const std::filesystem::path& input_video, const std::string& output) {
    try {
      const auto target = resolve_binary_output_path(output, "pv.mp4");
      if (!maiconv::convert_dat_or_usm_to_mp4(input_video, target)) {
        throw std::runtime_error("Video conversion failed: " + input_video.string() + " -> " + target.string());
      }
      std::cout << "Successfully converted at: " << target.string() << "\n";
      return kSuccess;
    }
    catch (const std::exception& ex) {
      std::cerr << "Program cannot proceed because of following error returned:\n" << ex.what() << "\n";
      return kFailure;
    }
  }

  std::optional<maiconv::AssetsExportLayout> parse_assets_export_layout(const std::string& value) {
    const std::string normalized = maiconv::lower(value);
    if (normalized == "flat") {
      return maiconv::AssetsExportLayout::Flat;
    }
    if (normalized == "genre") {
      return maiconv::AssetsExportLayout::Genre;
    }
    if (normalized == "version") {
      return maiconv::AssetsExportLayout::Version;
    }
    return std::nullopt;
  }

  std::optional<maiconv::AssetsLogLevel> parse_assets_log_level(const std::string& value) {
    const std::string normalized = maiconv::lower(value);
    if (normalized == "quiet") {
      return maiconv::AssetsLogLevel::Quiet;
    }
    if (normalized == "normal") {
      return maiconv::AssetsLogLevel::Normal;
    }
    if (normalized == "verbose") {
      return maiconv::AssetsLogLevel::Verbose;
    }
    return std::nullopt;
  }

}  // namespace

int main(int argc, char** argv) {
  CLI::App app{ "maiconv - Simai/ma2 cross-platform converter" };

  int exit_code = kSuccess;

  std::string ma2_input;
  std::string ma2_format_str = "simai";
  std::string ma2_rotate_str;
  int ma2_shift = 0;
  std::string ma2_output;
  auto* ma2_cmd = app.add_subcommand("ma2", "Convert MA2 chart to target format");
  ma2_cmd->add_option("--input", ma2_input, "Input ma2 file path")->required();
  ma2_cmd->add_option("--format", ma2_format_str, "simai|simai-fes|maidata|ma2-103|ma2-104");
  ma2_cmd->add_option("--rotate", ma2_rotate_str,
    "UpSideDown|Clockwise90|Clockwise180|Counterclockwise90|Counterclockwise180|LeftToRight");
  ma2_cmd->add_option("--shift", ma2_shift, "Shift by ticks");
  ma2_cmd->add_option("--output", ma2_output, "Output path (file or folder)");
  ma2_cmd->callback([&]() {
    const auto fmt = maiconv::parse_chart_format(ma2_format_str);
    if (!fmt.has_value()) {
      throw CLI::ValidationError("--format", "unsupported format: " + ma2_format_str);
    }
    std::optional<maiconv::FlipMethod> rotate;
    if (!ma2_rotate_str.empty()) {
      rotate = maiconv::parse_flip_method(ma2_rotate_str);
      if (!rotate.has_value()) {
        throw CLI::ValidationError("--rotate", "unsupported rotate method: " + ma2_rotate_str);
      }
    }
    exit_code = run_ma2_to(ma2_input, *fmt, rotate, ma2_shift, ma2_output);
    });

  std::string simai_input;
  std::string simai_format_str = "ma2-103";
  std::string simai_rotate_str;
  int simai_shift = 0;
  std::string simai_output;
  int simai_difficulty = 1;
  auto* simai_cmd = app.add_subcommand("simai", "Convert Simai chart to target format");
  simai_cmd->add_option("--input", simai_input, "Input simai file path")->required();
  auto* simai_diff_opt =
    simai_cmd->add_option("--difficulty", simai_difficulty, "Difficulty 1..7")->check(CLI::Range(1, 7));
  simai_cmd->add_option("--format", simai_format_str, "simai|simai-fes|maidata|ma2-103|ma2-104");
  simai_cmd->add_option("--rotate", simai_rotate_str,
    "UpSideDown|Clockwise90|Clockwise180|Counterclockwise90|Counterclockwise180|LeftToRight");
  simai_cmd->add_option("--shift", simai_shift, "Shift by ticks");
  simai_cmd->add_option("--output", simai_output, "Output path (file or folder)");
  simai_cmd->callback([&]() {
    const auto fmt = maiconv::parse_chart_format(simai_format_str);
    if (!fmt.has_value()) {
      throw CLI::ValidationError("--format", "unsupported format: " + simai_format_str);
    }
    std::optional<maiconv::FlipMethod> rotate;
    if (!simai_rotate_str.empty()) {
      rotate = maiconv::parse_flip_method(simai_rotate_str);
      if (!rotate.has_value()) {
        throw CLI::ValidationError("--rotate", "unsupported rotate method: " + simai_rotate_str);
      }
    }

    std::optional<int> diff;
    if (simai_diff_opt->count() > 0) {
      diff = simai_difficulty;
    }
    exit_code = run_simai_to(simai_input, diff, *fmt, rotate, simai_shift, simai_output);
    });

  std::string assets_input;
  std::string assets_output;
  std::string assets_id;
  int assets_difficulty = 0;
  std::string assets_music;
  std::string assets_cover;
  std::string assets_video;
  std::string assets_format_str = "simai";
  std::string assets_layout_str = "flat";
  std::string assets_rotate_str;
  int assets_shift = 0;
  bool assets_display = false;
  bool assets_decimal = false;
  bool assets_ignore = false;
  bool assets_number = false;
  bool assets_json = false;
  bool assets_zip = false;
  bool assets_collection = false;
  std::string assets_log_level_str = "normal";
  auto* assets_cmd =
    app.add_subcommand("assets", "Export from StreamingAssets (all, one id, or one id+difficulty)");
  assets_cmd->add_option("--input", assets_input, "Input path (StreamingAssets root)")->required();
  assets_cmd->add_option("--output", assets_output, "Output directory")->required();
  auto* assets_id_opt =
    assets_cmd->add_option("--id", assets_id,
      "Music id (optional, when omitted export all tracks)");
  auto* assets_diff_opt =
    assets_cmd->add_option("--difficulty", assets_difficulty,
      "Difficulty 1..7 using exported maidata numbering (optional, with --id set and no --difficulty export all difficulties)")
    ->check(CLI::Range(1, 7));
  assets_cmd->add_option("--music", assets_music, "Override music folder (default: auto-detect)");
  assets_cmd->add_option("--cover", assets_cover, "Override cover folder (default: auto-detect)");
  assets_cmd->add_option("--video", assets_video, "Override video folder (default: auto-detect)");
  assets_cmd->add_option("--format", assets_format_str, "simai|simai-fes|maidata|ma2-103|ma2-104");
  assets_cmd->add_option("--layout", assets_layout_str, "flat|genre|version");
  assets_cmd->add_flag("--display", assets_display, "Export maidata lv_* using display levels instead of constants");
  assets_cmd->add_option("--rotate", assets_rotate_str,
    "UpSideDown|Clockwise90|Clockwise180|Counterclockwise90|Counterclockwise180|LeftToRight");
  assets_cmd->add_option("--shift", assets_shift, "Shift by ticks");
  assets_cmd->add_flag("--decimal", assets_decimal, "Use decimal levels");
  assets_cmd->add_flag("--ignore", assets_ignore, "Ignore incomplete assets");
  assets_cmd->add_flag("--number", assets_number, "Use music id as folder name");
  assets_cmd->add_flag("--json", assets_json, "Write _index.json");
  assets_cmd->add_flag("--zip", assets_zip, "Zip output folders");
  assets_cmd->add_flag("--collection", assets_collection, "Write collection manifests");
  assets_cmd->add_option("--verbosity,--log-level", assets_log_level_str,
    "Console output level: quiet|normal|verbose");
  assets_cmd->callback([&]() {
    const auto fmt = maiconv::parse_chart_format(assets_format_str);
    if (!fmt.has_value()) {
      throw CLI::ValidationError("--format", "unsupported format: " + assets_format_str);
    }
    const auto layout = parse_assets_export_layout(assets_layout_str);
    if (!layout.has_value()) {
      throw CLI::ValidationError("--layout", "unsupported layout: " + assets_layout_str);
    }
    const auto log_level = parse_assets_log_level(assets_log_level_str);
    if (!log_level.has_value()) {
      throw CLI::ValidationError("--verbosity", "unsupported log level: " + assets_log_level_str);
    }
    std::optional<maiconv::FlipMethod> rotate;
    if (!assets_rotate_str.empty()) {
      rotate = maiconv::parse_flip_method(assets_rotate_str);
      if (!rotate.has_value()) {
        throw CLI::ValidationError("--rotate", "unsupported rotate method: " + assets_rotate_str);
      }
    }

    maiconv::AssetsOptions options;
    options.streaming_assets_path = assets_input;
    options.output_path = assets_output;
    if (assets_id_opt->count() > 0 && !assets_id.empty()) {
      options.target_music_id = assets_id;
    }
    if (assets_diff_opt->count() > 0) {
      options.target_difficulty = assets_difficulty;
    }
    if (!assets_music.empty()) {
      options.music_path = assets_music;
    }
    if (!assets_cover.empty()) {
      options.cover_path = assets_cover;
    }
    if (!assets_video.empty()) {
      options.video_path = assets_video;
    }

    options.format = *fmt;
    options.export_layout = *layout;
    options.maidata_level_mode = assets_display ? maiconv::MaidataLevelMode::Display
      : maiconv::MaidataLevelMode::Constant;
    options.rotate = rotate;
    options.shift_ticks = assets_shift;
    options.strict_decimal = assets_decimal;
    options.ignore_incomplete_assets = assets_ignore;
    options.music_id_folder_name = assets_number;
    options.log_tracks_json = assets_json;
    options.export_zip = assets_zip;
    options.compile_collections = assets_collection;
    options.log_level = *log_level;

    exit_code = maiconv::run_compile_assets(options);
    });

  std::string media_audio_acb;
  std::string media_audio_awb;
  std::string media_audio_output;
  std::string media_cover_input;
  std::string media_cover_output;
  std::string media_video_input;
  std::string media_video_output;

  auto* media_cmd = app.add_subcommand("media", "Standalone media transcode commands");
  auto* media_audio_cmd = media_cmd->add_subcommand("audio", "Convert ACB+AWB to MP3");
  media_audio_cmd->add_option("--acb", media_audio_acb, "Input .acb path")->required();
  media_audio_cmd->add_option("--awb", media_audio_awb, "Input .awb path")->required();
  media_audio_cmd->add_option("--output", media_audio_output,
    "Output file or directory (default: ./track.mp3)");
  media_audio_cmd->callback([&]() {
    const auto acb_ext = maiconv::lower(std::filesystem::path(media_audio_acb).extension().string());
    const auto awb_ext = maiconv::lower(std::filesystem::path(media_audio_awb).extension().string());
    if (acb_ext != ".acb") {
      throw CLI::ValidationError("--acb", "expected .acb file");
    }
    if (awb_ext != ".awb") {
      throw CLI::ValidationError("--awb", "expected .awb file");
    }
    exit_code = run_media_audio_to_mp3(media_audio_acb, media_audio_awb, media_audio_output);
    });

  auto* media_cover_cmd = media_cmd->add_subcommand("cover", "Convert jacket .ab to bg.png");
  media_cover_cmd->add_option("--input", media_cover_input, "Input .ab path")->required();
  media_cover_cmd->add_option("--output", media_cover_output,
    "Output file or directory (default: ./bg.png)");
  media_cover_cmd->callback([&]() {
    const auto ext = maiconv::lower(std::filesystem::path(media_cover_input).extension().string());
    if (ext != ".ab") {
      throw CLI::ValidationError("--input", "expected .ab file");
    }
    exit_code = run_media_cover_to_png(media_cover_input, media_cover_output);
    });

  auto* media_video_cmd = media_cmd->add_subcommand("video", "Convert .dat/.usm to pv.mp4");
  media_video_cmd->add_option("--input", media_video_input, "Input .dat/.usm path")->required();
  media_video_cmd->add_option("--output", media_video_output,
    "Output file or directory (default: ./pv.mp4)");
  media_video_cmd->callback([&]() {
    const auto ext = maiconv::lower(std::filesystem::path(media_video_input).extension().string());
    if (ext != ".dat" && ext != ".usm") {
      throw CLI::ValidationError("--input", "expected .dat or .usm file");
    }
    exit_code = run_media_video_to_mp4(media_video_input, media_video_output);
    });
  media_cmd->require_subcommand(1);

  try {
    app.require_subcommand(1);
    CLI11_PARSE(app, argc, argv);
  }
  catch (const CLI::ParseError& e) {
    return app.exit(e);
  }

  return exit_code;
}
