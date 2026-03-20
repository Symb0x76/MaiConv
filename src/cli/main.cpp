#include "maiconv/core/assets.hpp"
#include "maiconv/core/io.hpp"
#include "maiconv/core/ma2.hpp"
#include "maiconv/core/media/media_audio.hpp"
#include "maiconv/core/media/media_cover.hpp"
#include "maiconv/core/media/media_video.hpp"
#include "maiconv/core/simai/compiler.hpp"
#include "maiconv/core/simai/parser.hpp"
#include "maiconv/core/simai/tokenizer.hpp"

#include <CLI/CLI.hpp>

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace
{

  constexpr int kSuccess = 0;
  constexpr int kFailure = 2;

  std::filesystem::path
  resolve_output_path(const std::string &output,
                      const std::string &default_file_name)
  {
    std::filesystem::path out(output);
    if (out.extension().empty())
    {
      return out / default_file_name;
    }
    return out;
  }

  std::filesystem::path
  resolve_binary_output_path(const std::string &output,
                             const std::string &default_file_name)
  {
    if (output.empty())
    {
      return std::filesystem::current_path() / default_file_name;
    }
    return resolve_output_path(output, default_file_name);
  }

  bool has_non_empty_env(const char *name)
  {
#if defined(_WIN32)
    char *value = nullptr;
    std::size_t value_len = 0;
    const errno_t rc = _dupenv_s(&value, &value_len, name);
    if (rc != 0 || value == nullptr)
    {
      if (value != nullptr)
      {
        std::free(value);
      }
      return false;
    }
    const bool non_empty = value[0] != '\0';
    std::free(value);
    return non_empty;
#else
    const char *value = std::getenv(name);
    return value != nullptr && value[0] != '\0';
#endif
  }

  void set_process_env(const char *name, const char *value)
  {
#if defined(_WIN32)
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
  }

  void set_env_if_missing(const char *name, const char *value)
  {
    if (!has_non_empty_env(name))
    {
      set_process_env(name, value);
    }
  }

  void enable_ffmpeg_gpu_mode()
  {
    set_process_env("MAICONV_FFMPEG_GPU", "1");
    set_env_if_missing("MAICONV_FFMPEG_HWACCEL", "auto");
    set_env_if_missing("MAICONV_FFMPEG_AUDIO_HWACCEL", "auto");
  }

  void write_or_stdout(const std::string &output,
                       const std::string &default_file_name,
                       const std::string &payload)
  {
    if (output.empty())
    {
      std::cout << payload;
      if (payload.empty() || payload.back() != '\n')
      {
        std::cout << "\n";
      }
      return;
    }
    const auto target = resolve_output_path(output, default_file_name);
    std::string persisted(payload);
    persisted += "\r\n";
    maiconv::write_text_file(target, persisted);
    std::cout << "Successfully compiled at: " << target.string() << "\n";
  }

  int run_ma2_to(const std::filesystem::path &input, maiconv::ChartFormat format,
                 const std::optional<maiconv::FlipMethod> &rotate,
                 int shift_ticks, const std::string &output)
  {
    try
    {
      maiconv::Ma2Tokenizer tokenizer;
      maiconv::Ma2Parser parser;
      maiconv::Ma2Composer ma2_composer;
      maiconv::simai::Compiler simai_composer;

      maiconv::Chart chart = parser.parse(tokenizer.tokenize_file(input));
      if (rotate.has_value())
      {
        chart.rotate(*rotate);
      }
      if (shift_ticks != 0)
      {
        chart.shift_by_offset(shift_ticks);
      }

      std::string result;
      std::string file_name;
      if (format == maiconv::ChartFormat::Simai ||
          format == maiconv::ChartFormat::SimaiFes)
      {
        result = simai_composer.compile_chart(chart);
        file_name = "maidata.txt";
      }
      else if (format == maiconv::ChartFormat::Maidata)
      {
        result = "&inote_1=\n" + simai_composer.compile_chart(chart) + "\n";
        file_name = "maidata.txt";
      }
      else
      {
        result = ma2_composer.compose(chart, format);
        file_name = "result.ma2";
      }

      write_or_stdout(output, file_name, result);
      return kSuccess;
    }
    catch (const std::exception &ex)
    {
      std::cerr << "Program cannot proceed because of following error returned:\n"
                << ex.what() << "\n";
      return kFailure;
    }
  }

  int run_simai_to(const std::filesystem::path &input,
                   std::optional<int> difficulty, maiconv::ChartFormat format,
                   const std::optional<maiconv::FlipMethod> &rotate,
                   int shift_ticks, const std::string &output)
  {
    try
    {
      const int diff = difficulty.value_or(1);
      maiconv::simai::Tokenizer tokenizer;
      maiconv::simai::Parser parser;
      maiconv::Ma2Composer ma2_composer;
      maiconv::simai::Compiler simai_composer;

      const auto doc = tokenizer.parse_file(input);
      maiconv::Chart chart = parser.parse_document(doc, diff);

      if (rotate.has_value())
      {
        chart.rotate(*rotate);
      }
      if (shift_ticks != 0)
      {
        chart.shift_by_offset(shift_ticks);
      }

      std::string result;
      std::string file_name;
      if (format == maiconv::ChartFormat::Simai ||
          format == maiconv::ChartFormat::SimaiFes)
      {
        result = simai_composer.compile_chart(chart);
        file_name = "maidata.txt";
      }
      else if (format == maiconv::ChartFormat::Maidata)
      {
        result = "&inote_" + std::to_string(diff) + "=\n" +
                 simai_composer.compile_chart(chart) + "\n";
        file_name = "maidata.txt";
      }
      else
      {
        result = ma2_composer.compose(chart, format);
        file_name = "result.ma2";
      }

      write_or_stdout(output, file_name, result);
      return kSuccess;
    }
    catch (const std::exception &ex)
    {
      std::cerr << "Program cannot proceed because of following error returned:\n"
                << ex.what() << "\n";
      return kFailure;
    }
  }

  int run_media_audio_to_mp3(const std::filesystem::path &acb,
                             const std::filesystem::path &awb,
                             const std::string &output)
  {
    try
    {
      const auto target = resolve_binary_output_path(output, "track.mp3");
      if (!maiconv::convert_acb_awb_to_mp3(acb, awb, target))
      {
        throw std::runtime_error("Audio conversion failed: " + acb.string() +
                                 " + " + awb.string() + " -> " + target.string());
      }
      std::cout << "Successfully converted at: " << target.string() << "\n";
      return kSuccess;
    }
    catch (const std::exception &ex)
    {
      std::cerr << "Program cannot proceed because of following error returned:\n"
                << ex.what() << "\n";
      return kFailure;
    }
  }

  int run_media_audio_file_to_mp3(const std::filesystem::path &input_audio,
                                  const std::string &output)
  {
    try
    {
      const auto target = resolve_binary_output_path(output, "track.mp3");
      if (!maiconv::convert_audio_to_mp3(input_audio, target))
      {
        throw std::runtime_error("Audio conversion failed: " +
                                 input_audio.string() + " -> " + target.string());
      }
      std::cout << "Successfully converted at: " << target.string() << "\n";
      return kSuccess;
    }
    catch (const std::exception &ex)
    {
      std::cerr << "Program cannot proceed because of following error returned:\n"
                << ex.what() << "\n";
      return kFailure;
    }
  }

  int run_media_mp3_to_acb_awb(const std::filesystem::path &input_mp3,
                               const std::string &output_acb,
                               const std::string &output_awb)
  {
    try
    {
      const auto target_acb = resolve_binary_output_path(output_acb, "track.acb");
      std::filesystem::path target_awb;
      if (output_awb.empty())
      {
        target_awb = target_acb;
        target_awb.replace_extension(".awb");
      }
      else
      {
        target_awb = resolve_binary_output_path(output_awb, "track.awb");
      }

      if (!maiconv::convert_mp3_to_acb_awb(input_mp3, target_acb, target_awb))
      {
        throw std::runtime_error(
            "Audio conversion failed: " + input_mp3.string() + " -> " +
            target_acb.string() + " + " + target_awb.string());
      }

      std::cout << "Successfully converted at:\n"
                << "  ACB: " << target_acb.string() << "\n"
                << "  AWB: " << target_awb.string() << "\n";
      return kSuccess;
    }
    catch (const std::exception &ex)
    {
      std::cerr << "Program cannot proceed because of following error returned:\n"
                << ex.what() << "\n";
      return kFailure;
    }
  }

  int run_media_cover_to_png(const std::filesystem::path &input_ab,
                             const std::string &output)
  {
    try
    {
      const auto target = resolve_binary_output_path(output, "bg.png");
      if (!maiconv::convert_ab_to_png(input_ab, target))
      {
        throw std::runtime_error("Cover conversion failed: " + input_ab.string() +
                                 " -> " + target.string());
      }
      std::cout << "Successfully converted at: " << target.string() << "\n";
      return kSuccess;
    }
    catch (const std::exception &ex)
    {
      std::cerr << "Program cannot proceed because of following error returned:\n"
                << ex.what() << "\n";
      return kFailure;
    }
  }

  int run_media_cover_to_ab(const std::filesystem::path &input_image,
                            const std::string &output)
  {
    try
    {
      const auto target = resolve_binary_output_path(output, "bg.ab");
      if (!target.parent_path().empty())
      {
        std::filesystem::create_directories(target.parent_path());
      }
      std::filesystem::copy_file(
          input_image, target, std::filesystem::copy_options::overwrite_existing);
      if (!std::filesystem::exists(target) ||
          std::filesystem::file_size(target) == 0)
      {
        throw std::runtime_error("Cover conversion failed: " +
                                 input_image.string() + " -> " + target.string());
      }
      std::cout << "Successfully converted at: " << target.string() << "\n";
      return kSuccess;
    }
    catch (const std::exception &ex)
    {
      std::cerr << "Program cannot proceed because of following error returned:\n"
                << ex.what() << "\n";
      return kFailure;
    }
  }

  int run_media_video_to_mp4(const std::filesystem::path &input_video,
                             const std::string &output)
  {
    try
    {
      const auto target = resolve_binary_output_path(output, "pv.mp4");
      if (!maiconv::convert_dat_or_usm_to_mp4(input_video, target))
      {
        throw std::runtime_error("Video conversion failed: " +
                                 input_video.string() + " -> " + target.string());
      }
      std::cout << "Successfully converted at: " << target.string() << "\n";
      return kSuccess;
    }
    catch (const std::exception &ex)
    {
      std::cerr << "Program cannot proceed because of following error returned:\n"
                << ex.what() << "\n";
      return kFailure;
    }
  }

  int run_media_video_to_dat(const std::filesystem::path &input_mp4,
                             const std::string &output)
  {
    try
    {
      const auto target = resolve_binary_output_path(output, "pv.dat");
      if (!maiconv::convert_mp4_to_dat(input_mp4, target))
      {
        throw std::runtime_error(
            "Video conversion failed: " + input_mp4.string() + " -> " +
            target.string() + " (requires ffmpeg with VP9 encoder in PATH)");
      }
      std::cout << "Successfully converted at: " << target.string() << "\n";
      return kSuccess;
    }
    catch (const std::exception &ex)
    {
      std::cerr << "Program cannot proceed because of following error returned:\n"
                << ex.what() << "\n";
      return kFailure;
    }
  }

  std::optional<maiconv::AssetsExportLayout>
  parse_assets_export_layout(const std::string &value)
  {
    const std::string normalized = maiconv::lower(value);
    if (normalized == "flat")
    {
      return maiconv::AssetsExportLayout::Flat;
    }
    if (normalized == "genre")
    {
      return maiconv::AssetsExportLayout::Genre;
    }
    if (normalized == "version")
    {
      return maiconv::AssetsExportLayout::Version;
    }
    return std::nullopt;
  }

  std::optional<maiconv::AssetsLogLevel>
  parse_assets_log_level(const std::string &value)
  {
    const std::string normalized = maiconv::lower(value);
    if (normalized == "quiet")
    {
      return maiconv::AssetsLogLevel::Quiet;
    }
    if (normalized == "normal")
    {
      return maiconv::AssetsLogLevel::Normal;
    }
    if (normalized == "verbose")
    {
      return maiconv::AssetsLogLevel::Verbose;
    }
    return std::nullopt;
  }

  std::string trim_copy(std::string_view value)
  {
    std::size_t begin = 0;
    while (begin < value.size() &&
           std::isspace(static_cast<unsigned char>(value[begin])) != 0)
    {
      ++begin;
    }

    std::size_t end = value.size();
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(value[end - 1])) != 0)
    {
      --end;
    }
    return std::string(value.substr(begin, end - begin));
  }

  bool apply_assets_export_type_token(maiconv::AssetsOptions &options,
                                      const std::string &token)
  {
    if (token == "chart" || token == "maidata" || token == "maidata.txt" ||
        token == "ma2" || token == "result.ma2")
    {
      options.export_chart = true;
      return true;
    }
    if (token == "audio" || token == "music" || token == "track.mp3")
    {
      options.export_audio = true;
      return true;
    }
    if (token == "cover" || token == "jacket" || token == "bg" ||
        token == "bg.png")
    {
      options.export_cover = true;
      return true;
    }
    if (token == "video" || token == "movie" || token == "pv" ||
        token == "pv.mp4")
    {
      options.export_video = true;
      return true;
    }
    return false;
  }

} // namespace

int main(int argc, char **argv)
{
  std::cout.setf(std::ios::unitbuf);
  std::cerr.setf(std::ios::unitbuf);

  CLI::App app{"maiconv - Simai/ma2 cross-platform converter"};

  int exit_code = kSuccess;

  std::string ma2_input;
  std::string ma2_format_str = "simai";
  std::string ma2_rotate_str;
  int ma2_shift = 0;
  std::string ma2_output;
  auto *ma2_cmd =
      app.add_subcommand("ma2", "Convert MA2 chart to target format");
  ma2_cmd->add_option("--input", ma2_input, "Input ma2 file path")->required();
  ma2_cmd->add_option("--format", ma2_format_str,
                      "simai|simai-fes|maidata|ma2|ma2-103|ma2-104");
  ma2_cmd->add_option("--rotate", ma2_rotate_str,
                      "UpSideDown|Clockwise90|Clockwise180|Counterclockwise90|"
                      "Counterclockwise180|LeftToRight");
  ma2_cmd->add_option("--shift", ma2_shift, "Shift by ticks");
  ma2_cmd->add_option("--output", ma2_output, "Output path (file or folder)");
  ma2_cmd->callback([&]()
                    {
    const auto fmt = maiconv::parse_chart_format(ma2_format_str);
    if (!fmt.has_value()) {
      throw CLI::ValidationError("--format",
                                 "unsupported format: " + ma2_format_str);
    }
    std::optional<maiconv::FlipMethod> rotate;
    if (!ma2_rotate_str.empty()) {
      rotate = maiconv::parse_flip_method(ma2_rotate_str);
      if (!rotate.has_value()) {
        throw CLI::ValidationError("--rotate", "unsupported rotate method: " +
                                                   ma2_rotate_str);
      }
    }
    exit_code = run_ma2_to(ma2_input, *fmt, rotate, ma2_shift, ma2_output); });

  std::string simai_input;
  std::string simai_format_str = "ma2-103";
  std::string simai_rotate_str;
  int simai_shift = 0;
  std::string simai_output;
  int simai_difficulty = 1;
  auto *simai_cmd =
      app.add_subcommand("simai", "Convert Simai chart to target format");
  simai_cmd->add_option("--input", simai_input, "Input simai file path")
      ->required();
  auto *simai_diff_opt =
      simai_cmd->add_option("--difficulty", simai_difficulty, "Difficulty 1..7")
          ->check(CLI::Range(1, 7));
  simai_cmd->add_option("--format", simai_format_str,
                        "simai|simai-fes|maidata|ma2|ma2-103|ma2-104");
  simai_cmd->add_option("--rotate", simai_rotate_str,
                        "UpSideDown|Clockwise90|Clockwise180|"
                        "Counterclockwise90|Counterclockwise180|LeftToRight");
  simai_cmd->add_option("--shift", simai_shift, "Shift by ticks");
  simai_cmd->add_option("--output", simai_output,
                        "Output path (file or folder)");
  simai_cmd->callback([&]()
                      {
    const auto fmt = maiconv::parse_chart_format(simai_format_str);
    if (!fmt.has_value()) {
      throw CLI::ValidationError("--format",
                                 "unsupported format: " + simai_format_str);
    }
    std::optional<maiconv::FlipMethod> rotate;
    if (!simai_rotate_str.empty()) {
      rotate = maiconv::parse_flip_method(simai_rotate_str);
      if (!rotate.has_value()) {
        throw CLI::ValidationError("--rotate", "unsupported rotate method: " +
                                                   simai_rotate_str);
      }
    }

    std::optional<int> diff;
    if (simai_diff_opt->count() > 0) {
      diff = simai_difficulty;
    }
    exit_code = run_simai_to(simai_input, diff, *fmt, rotate, simai_shift,
                             simai_output); });

  std::string assets_input;
  std::string assets_output;
  std::string assets_id;
  std::string assets_difficulty;
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
  bool assets_dummy = false;
  bool assets_resume = false;
  bool assets_number = false;
  bool assets_json = false;
  bool assets_zip = false;
  bool assets_collection = false;
  bool assets_gpu = false;
  int assets_jobs = 1;
  bool assets_timing = false;
  std::string assets_log_level_str = "normal";
  std::vector<std::string> assets_types;
  auto *assets_cmd = app.add_subcommand(
      "assets",
      "Export from StreamingAssets (all, one id, or one id+difficulty)");
  assets_cmd
      ->add_option("--input", assets_input, "Input path (StreamingAssets root)")
      ->required();
  assets_cmd->add_option("--output", assets_output, "Output directory")
      ->required();
  auto *assets_id_opt = assets_cmd->add_option(
      "--id", assets_id,
      "Music id filter (optional; comma-separated values/regex)");
  auto *assets_diff_opt = assets_cmd->add_option(
      "--difficulty", assets_difficulty,
      "Difficulty filter using exported maidata numbering 1..7 "
      "(optional; comma-separated values/regex, with --id set and "
      "no --difficulty export all difficulties)");
  assets_cmd->add_option("--music", assets_music,
                         "Override music folder (default: auto-detect)");
  assets_cmd->add_option("--cover", assets_cover,
                         "Override cover folder (default: auto-detect)");
  assets_cmd->add_option("--video", assets_video,
                         "Override video folder (default: auto-detect)");
  assets_cmd->add_option("--format", assets_format_str,
                         "simai|simai-fes|maidata|ma2|ma2-103|ma2-104");
  assets_cmd->add_option("--layout", assets_layout_str, "flat|genre|version");
  assets_cmd->add_flag(
      "--display", assets_display,
      "Export maidata lv_* using display levels instead of constants");
  assets_cmd->add_option("--rotate", assets_rotate_str,
                         "UpSideDown|Clockwise90|Clockwise180|"
                         "Counterclockwise90|Counterclockwise180|LeftToRight");
  assets_cmd->add_option("--shift", assets_shift, "Shift by ticks");
  assets_cmd->add_flag("--decimal", assets_decimal, "Use decimal levels");
  assets_cmd->add_flag("--ignore", assets_ignore, "Ignore incomplete assets");
  assets_cmd->add_flag(
      "--dummy", assets_dummy,
      "Generate dummy track.mp3/pv.mp4 when source media is missing");
  assets_cmd->add_flag("--resume,--skip-existing", assets_resume,
                       "Skip tracks that already have a complete export");
  assets_cmd->add_flag("--number", assets_number,
                       "Use music id as folder name");
  assets_cmd->add_flag("--json", assets_json, "Write _index.json");
  assets_cmd->add_flag("--zip", assets_zip, "Zip output folders");
  assets_cmd->add_flag("--collection", assets_collection,
                       "Write collection manifests");
  assets_cmd->add_flag(
      "--gpu", assets_gpu,
      "Enable automatic ffmpeg GPU acceleration hints and encoder fallback");
  assets_cmd
      ->add_option("--jobs", assets_jobs,
                   "Worker count for track-level parallel export")
      ->check(CLI::Range(1, 128));
  assets_cmd->add_flag("--timing", assets_timing,
                       "Emit phase timing summary (aggregate + p95)");
  assets_cmd
      ->add_option("--types", assets_types,
                   "Export type filter (comma-separated): "
                   "maidata.txt|track.mp3|bg.png|pv.mp4")
      ->delimiter(',');
  assets_cmd->add_option("--verbosity,--log-level", assets_log_level_str,
                         "Console output level: quiet|normal|verbose");
  assets_cmd->callback([&]()
                       {
    if (assets_gpu) {
      enable_ffmpeg_gpu_mode();
    }

    const auto fmt = maiconv::parse_chart_format(assets_format_str);
    if (!fmt.has_value()) {
      throw CLI::ValidationError("--format",
                                 "unsupported format: " + assets_format_str);
    }
    const auto layout = parse_assets_export_layout(assets_layout_str);
    if (!layout.has_value()) {
      throw CLI::ValidationError("--layout",
                                 "unsupported layout: " + assets_layout_str);
    }
    const auto log_level = parse_assets_log_level(assets_log_level_str);
    if (!log_level.has_value()) {
      throw CLI::ValidationError("--verbosity", "unsupported log level: " +
                                                    assets_log_level_str);
    }
    std::optional<maiconv::FlipMethod> rotate;
    if (!assets_rotate_str.empty()) {
      rotate = maiconv::parse_flip_method(assets_rotate_str);
      if (!rotate.has_value()) {
        throw CLI::ValidationError("--rotate", "unsupported rotate method: " +
                                                   assets_rotate_str);
      }
    }

    maiconv::AssetsOptions options;
    options.streaming_assets_path = assets_input;
    options.output_path = assets_output;
    if (assets_id_opt->count() > 0 && !assets_id.empty()) {
      options.target_music_filters.push_back(assets_id);
    }
    if (assets_diff_opt->count() > 0 && !assets_difficulty.empty()) {
      options.target_difficulty_filters.push_back(assets_difficulty);
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
    options.maidata_level_mode = assets_display
                                     ? maiconv::MaidataLevelMode::Display
                                     : maiconv::MaidataLevelMode::Constant;
    options.rotate = rotate;
    options.shift_ticks = assets_shift;
    options.strict_decimal = assets_decimal;
    options.ignore_incomplete_assets = assets_ignore;
    options.dummy_assets = assets_dummy;
    options.skip_existing_exports = assets_resume;
    options.music_id_folder_name = assets_number;
    options.log_tracks_json = assets_json;
    options.export_zip = assets_zip;
    options.compile_collections = assets_collection;
    options.log_level = *log_level;
    options.jobs = assets_jobs;
    options.enable_timing = assets_timing;
    if (!assets_types.empty()) {
      options.export_chart = false;
      options.export_audio = false;
      options.export_cover = false;
      options.export_video = false;

      for (const auto &raw_token : assets_types) {
        const std::string token = maiconv::lower(trim_copy(raw_token));
        if (token.empty()) {
          throw CLI::ValidationError("--types", "contains empty type item");
        }
        if (!apply_assets_export_type_token(options, token)) {
          throw CLI::ValidationError("--types",
                                     "unsupported export type: " + raw_token);
        }
      }
    }

    if (!options.export_chart && !options.export_audio &&
        !options.export_cover && !options.export_video) {
      throw CLI::ValidationError("--types",
                                 "at least one export type is required");
    }

    exit_code = maiconv::run_compile_assets(options); });

  std::string media_audio_acb;
  std::string media_audio_awb;
  std::string media_audio_input;
  std::string media_audio_output;
  std::string media_audio_output_awb;
  std::string media_cover_input;
  std::string media_cover_output;
  std::string media_video_input;
  std::string media_video_output;
  bool media_audio_gpu = false;
  bool media_video_gpu = false;

  auto *media_cmd =
      app.add_subcommand("media", "Standalone media transcode commands");
  auto *media_audio_cmd =
      media_cmd->add_subcommand("audio", "Convert audio to MP3");
  media_audio_cmd->add_option(
      "--input", media_audio_input,
      "Single input audio path (e.g. .mp3/.ogg/.wav/.awb/.acb when paired "
      "files are not required)");
  media_audio_cmd->add_option("--acb", media_audio_acb,
                              "Input .acb path (use with --awb)");
  media_audio_cmd->add_option("--awb", media_audio_awb,
                              "Input .awb path (use with --acb)");
  media_audio_cmd->add_option("--output", media_audio_output,
                              "Output file or directory (default: ./track.mp3 "
                              "for decode, ./track.acb for encode)");
  media_audio_cmd->add_option("--output-awb", media_audio_output_awb,
                              "Output .awb file or directory (only for .mp3 "
                              "input; default: same stem as --output)");
  media_audio_cmd->add_flag(
      "--gpu", media_audio_gpu,
      "Enable automatic ffmpeg GPU acceleration hints and encoder fallback");
  media_audio_cmd->callback([&]()
                            {
    if (media_audio_gpu) {
      enable_ffmpeg_gpu_mode();
    }

    const bool has_input = !media_audio_input.empty();
    const bool has_acb = !media_audio_acb.empty();
    const bool has_awb = !media_audio_awb.empty();
    if (!has_input && !(has_acb && has_awb)) {
      throw CLI::ValidationError(
          "--input", "provide either --input or both --acb and --awb");
    }
    if (has_input && (has_acb || has_awb)) {
      throw CLI::ValidationError(
          "--input", "--input cannot be used together with --acb/--awb");
    }

    if (has_input) {
      const auto input_ext = maiconv::lower(
          std::filesystem::path(media_audio_input).extension().string());
      if (input_ext == ".mp3") {
        if (!media_audio_output_awb.empty()) {
          const auto awb_ext =
              maiconv::lower(std::filesystem::path(media_audio_output_awb)
                                 .extension()
                                 .string());
          if (!awb_ext.empty() && awb_ext != ".awb") {
            throw CLI::ValidationError("--output-awb",
                                       "expected .awb file or directory");
          }
        }
        if (!media_audio_output.empty()) {
          const auto out_ext = maiconv::lower(
              std::filesystem::path(media_audio_output).extension().string());
          if (!out_ext.empty() && out_ext != ".acb") {
            throw CLI::ValidationError(
                "--output", "for .mp3 input expected .acb file or directory");
          }
        }
        exit_code = run_media_mp3_to_acb_awb(
            media_audio_input, media_audio_output, media_audio_output_awb);
        return;
      }

      if (!media_audio_output_awb.empty()) {
        throw CLI::ValidationError("--output-awb",
                                   "--output-awb is only valid for .mp3 input");
      }
      exit_code =
          run_media_audio_file_to_mp3(media_audio_input, media_audio_output);
      return;
    }

    const auto acb_ext = maiconv::lower(
        std::filesystem::path(media_audio_acb).extension().string());
    const auto awb_ext = maiconv::lower(
        std::filesystem::path(media_audio_awb).extension().string());
    if (acb_ext != ".acb") {
      throw CLI::ValidationError("--acb", "expected .acb file");
    }
    if (awb_ext != ".awb") {
      throw CLI::ValidationError("--awb", "expected .awb file");
    }
    exit_code = run_media_audio_to_mp3(media_audio_acb, media_audio_awb,
                                       media_audio_output); });

  auto *media_cover_cmd = media_cmd->add_subcommand(
      "cover", "Convert jacket between .ab and .png/.jpg/.jpeg");
  media_cover_cmd->add_option("--input", media_cover_input, "Input .ab path")
      ->required();
  media_cover_cmd->add_option("--output", media_cover_output,
                              "Output file or directory (default: based on "
                              "direction: ./bg.png or ./bg.ab)");
  media_cover_cmd->callback([&]()
                            {
    const auto in_ext = maiconv::lower(
        std::filesystem::path(media_cover_input).extension().string());
    std::string out_ext;
    if (!media_cover_output.empty()) {
      out_ext = maiconv::lower(
          std::filesystem::path(media_cover_output).extension().string());
    }

    const bool input_is_ab = in_ext == ".ab";
    const bool input_is_image =
        in_ext == ".png" || in_ext == ".jpg" || in_ext == ".jpeg";
    if (!input_is_ab && !input_is_image) {
      throw CLI::ValidationError("--input",
                                 "expected .ab/.png/.jpg/.jpeg file");
    }

    if (input_is_ab) {
      if (!out_ext.empty() && out_ext != ".png") {
        throw CLI::ValidationError(
            "--output", "for .ab input, output must be .png or a directory");
      }
      exit_code = run_media_cover_to_png(media_cover_input, media_cover_output);
      return;
    }

    if (!out_ext.empty() && out_ext != ".ab") {
      throw CLI::ValidationError(
          "--output", "for image input, output must be .ab or a directory");
    }
    exit_code = run_media_cover_to_ab(media_cover_input, media_cover_output); });

  auto *media_video_cmd = media_cmd->add_subcommand(
      "video", "Convert .dat/.usm/.crid -> pv.mp4, or .mp4 -> pv.dat");
  media_video_cmd
      ->add_option("--input", media_video_input,
                   "Input .dat/.usm/.crid/.mp4 path")
      ->required();
  media_video_cmd->add_option("--output", media_video_output,
                              "Output file or directory (default: based on "
                              "direction: ./pv.mp4 or ./pv.dat)");
  media_video_cmd->add_flag(
      "--gpu", media_video_gpu,
      "Enable automatic ffmpeg GPU acceleration hints and encoder fallback");
  media_video_cmd->callback([&]()
                            {
    if (media_video_gpu) {
      enable_ffmpeg_gpu_mode();
    }

    const auto ext = maiconv::lower(
        std::filesystem::path(media_video_input).extension().string());
    if (ext == ".dat" || ext == ".usm" || ext == ".crid") {
      exit_code = run_media_video_to_mp4(media_video_input, media_video_output);
      return;
    }

    if (ext == ".mp4") {
      exit_code = run_media_video_to_dat(media_video_input, media_video_output);
      return;
    }

    throw CLI::ValidationError("--input", "expected .dat/.usm/.crid/.mp4 file"); });
  media_cmd->require_subcommand(1);

  try
  {
    app.require_subcommand(1);
    CLI11_PARSE(app, argc, argv);
  }
  catch (const CLI::ParseError &e)
  {
    return app.exit(e);
  }

  return exit_code;
}
