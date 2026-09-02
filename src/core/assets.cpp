#include "maiconv/core/assets.hpp"
#include "maiconv/core/assets_internal.hpp"

#include "maiconv/core/chart.hpp"
#include "maiconv/core/io.hpp"
#include "maiconv/core/ma2.hpp"
#include "maiconv/core/media/media_audio.hpp"
#include "maiconv/core/media/media_cover.hpp"
#include "maiconv/core/media/media_video.hpp"
#include "maiconv/core/simai/compiler.hpp"
#include "maiconv/core/zip_util.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace maiconv {
namespace {

constexpr int kSuccess = 0;
constexpr int kFailure = 2;
constexpr const char *kDummyWarningPrefix = "MAICONV_DUMMY";
constexpr const char *kDummyTagMissingAudio = "MISSING_AUDIO";
constexpr const char *kDummyTagMissingVideo = "MISSING_VIDEO";
constexpr const char *kDummyTagSourceBgPng = "SOURCE_BG_PNG";
constexpr const char *kDummyTagBlackFrame = "BLACK_FRAME";

std::optional<std::string>
detect_image_extension_by_magic(const std::filesystem::path &path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return std::nullopt;
  }

  std::array<std::uint8_t, 12> head{};
  in.read(reinterpret_cast<char *>(head.data()),
          static_cast<std::streamsize>(head.size()));
  const std::streamsize read_bytes = in.gcount();
  if (read_bytes >= 8) {
    constexpr std::array<std::uint8_t, 8> kPngSig = {
        0x89U, 0x50U, 0x4EU, 0x47U, 0x0DU, 0x0AU, 0x1AU, 0x0AU};
    if (std::equal(kPngSig.begin(), kPngSig.end(), head.begin())) {
      return std::string(".png");
    }
  }
  if (read_bytes >= 3 && head[0] == 0xFFU && head[1] == 0xD8U &&
      head[2] == 0xFFU) {
    return std::string(".jpg");
  }

  return std::nullopt;
}

double estimate_chart_duration_seconds(const Chart &chart) {
  int max_tick = 0;
  for (const auto &note : chart.notes()) {
    int note_end = note.tick_stamp(chart.definition());
    if (note.wait_ticks > 0) {
      note_end += note.wait_ticks;
    }
    if (note.last_ticks > 0) {
      note_end += note.last_ticks;
    }
    if (note_end > max_tick) {
      max_tick = note_end;
    }
  }
  return chart.ticks_to_seconds(max_tick);
}

enum class UtagePlayerSide { None = 0, Left, Right };

bool has_case_insensitive_suffix(std::string_view text,
                                 std::string_view suffix) {
  if (text.size() < suffix.size()) {
    return false;
  }

  const std::size_t offset = text.size() - suffix.size();
  for (std::size_t i = 0; i < suffix.size(); ++i) {
    const unsigned char lhs = static_cast<unsigned char>(text[offset + i]);
    const unsigned char rhs = static_cast<unsigned char>(suffix[i]);
    if (std::tolower(lhs) != std::tolower(rhs)) {
      return false;
    }
  }
  return true;
}

UtagePlayerSide
detect_utage_player_side_from_chart(const std::filesystem::path &chart_path) {
  const std::string lower_name =
      lower(path_to_generic_utf8(chart_path.filename()));
  if (has_case_insensitive_suffix(lower_name, "_l.ma2")) {
    return UtagePlayerSide::Left;
  }
  if (has_case_insensitive_suffix(lower_name, "_r.ma2")) {
    return UtagePlayerSide::Right;
  }
  return UtagePlayerSide::None;
}

std::string utage_player_side_suffix(UtagePlayerSide side) {
  if (side == UtagePlayerSide::Left) {
    return " (L)";
  }
  if (side == UtagePlayerSide::Right) {
    return " (R)";
  }
  return "";
}

bool is_reserved_music_id(const std::string &id) {
  const int numeric_id = to_int(id, -1);
  return numeric_id == 0 || numeric_id == 1;
}

std::string music_folder_id_key(const std::filesystem::path &folder) {
  std::string folder_name = path_to_utf8(folder.filename());
  if (folder_name.rfind("music", 0) == 0) {
    folder_name = folder_name.substr(5);
  }
  return pad_music_id(folder_name, 6);
}

std::string join_tokens(const std::vector<std::string> &tokens) {
  std::size_t reserve_size = tokens.empty() ? 0 : (tokens.size() - 1);
  for (const auto &token : tokens) {
    reserve_size += token.size();
  }
  std::string out;
  out.reserve(reserve_size);
  for (std::size_t i = 0; i < tokens.size(); ++i) {
    if (i > 0) {
      out.push_back(',');
    }
    out += tokens[i];
  }
  return out;
}

std::unordered_map<std::string, std::filesystem::path>
find_asset_candidates_in_indexes(const std::vector<AssetIndex> &indexes,
                                 const std::vector<std::string> &names) {
  std::unordered_map<std::string, std::filesystem::path> found;
  if (names.empty() || indexes.empty()) {
    return found;
  }

  std::vector<std::string> lowered_names;
  lowered_names.reserve(names.size());
  for (const auto &name : names) {
    lowered_names.push_back(lower(name));
  }
  found.reserve(lowered_names.size());

  for (const auto &index : indexes) {
    for (const auto &name : lowered_names) {
      if (found.find(name) != found.end()) {
        continue;
      }
      const auto it = index.find(name);
      if (it != index.end()) {
        found.emplace(name, it->second);
      }
    }
    if (found.size() == lowered_names.size()) {
      break;
    }
  }

  return found;
}

void append_unique_string(std::vector<std::string> &values,
                          std::string candidate) {
  if (std::find(values.begin(), values.end(), candidate) == values.end()) {
    values.push_back(std::move(candidate));
  }
}

void append_suffix_candidates(const std::vector<std::string> &stems,
                              std::initializer_list<std::string_view> suffixes,
                              std::vector<std::string> &out) {
  out.clear();
  out.reserve(stems.size() * suffixes.size());
  for (const auto &stem : stems) {
    for (const auto suffix : suffixes) {
      out.push_back(stem + std::string(suffix));
    }
  }
}

void append_prefixed_suffix_candidates(
    const std::vector<std::string> &stems, std::string_view prefix,
    std::initializer_list<std::string_view> suffixes,
    std::vector<std::string> &out) {
  out.clear();
  out.reserve(stems.size() * suffixes.size());
  for (const auto &stem : stems) {
    const std::string prefixed = std::string(prefix) + stem;
    for (const auto suffix : suffixes) {
      out.push_back(prefixed + std::string(suffix));
    }
  }
}

std::filesystem::path first_found_candidate_in_order(
    const std::unordered_map<std::string, std::filesystem::path> &found,
    const std::vector<std::string> &ordered_candidates) {
  for (const auto &candidate : ordered_candidates) {
    const auto it = found.find(lower(candidate));
    if (it != found.end()) {
      return it->second;
    }
  }
  return {};
}

std::string category_name_for_layout(const TrackInfo &info,
                                     AssetsExportLayout layout) {
  const auto normalize_layout_genre = [](std::string_view genre) {
    static const std::array<std::pair<std::string_view, std::string_view>, 4>
        kGenreMappings = {{
            {"POPSアニメ", "POPS&アニメ"},
            {"niconicoボーカロイド", "niconico&ボーカロイド"},
            {"ゲームバラエティ", "ゲーム&バラエティ"},
            {"オンゲキCHUNITHM", "オンゲキ&CHUNITHM"},
        }};
    for (const auto &[from, to] : kGenreMappings) {
      if (genre == from) {
        return std::string(to);
      }
    }
    return std::string(genre);
  };

  const auto normalize_layout_version = [](std::string version) {
    if (version.empty()) {
      return version;
    }
    constexpr std::string_view kSuffix = "PLUS";
    if (version.size() < kSuffix.size()) {
      return version;
    }

    const std::size_t pos = version.size() - kSuffix.size();
    if (version.compare(pos, kSuffix.size(), kSuffix) != 0) {
      return version;
    }
    if (pos > 0 && version[pos - 1] == ' ') {
      return version;
    }

    version.insert(pos, " ");
    return version;
  };

  switch (layout) {
  case AssetsExportLayout::Flat:
    return "";
  case AssetsExportLayout::Genre:
    return info.genre.empty() ? "Unknown" : normalize_layout_genre(info.genre);
  case AssetsExportLayout::Version: {
    const auto completed =
        complete_version_fields(info.version_id, info.version);
    const std::string version =
        normalize_export_version_display(completed.second);
    return version.empty() ? "Unknown" : normalize_layout_version(version);
  }
  }
  return "";
}

std::string json_escape(const std::string &value) {
  std::string out;
  out.reserve(value.size());
  for (char c : value) {
    switch (c) {
    case '\\':
      out += "\\\\";
      break;
    case '"':
      out += "\\\"";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\t':
      out += "\\t";
      break;
    default:
      out.push_back(c);
      break;
    }
  }
  return out;
}

std::string normalize_maidata_metadata_value(std::string_view value) {
  std::string out;
  out.reserve(value.size());
  bool previous_was_space = false;
  for (char c : value) {
    if (c == '\r' || c == '\n' || c == '\t') {
      if (!previous_was_space) {
        out.push_back(' ');
        previous_was_space = true;
      }
      continue;
    }
    out.push_back(c);
    previous_was_space = (c == ' ');
  }
  return trim(out);
}

std::string maiconv_build_version_string() {
#ifdef MAICONV_BUILD_VERSION
  return MAICONV_BUILD_VERSION;
#else
  return "unknown";
#endif
}

std::string
export_display_title(const TrackInfo &info,
                     UtagePlayerSide utage_side = UtagePlayerSide::None) {
  std::string title = info.name.empty() ? info.sort_name : info.name;
  title = normalize_maidata_metadata_value(title);
  if (info.is_dx) {
    constexpr std::string_view suffix = " [DX]";
    if (title.size() < suffix.size() ||
        title.compare(title.size() - suffix.size(), suffix.size(), suffix) !=
            0) {
      title += suffix;
    }
  }

  const std::string side_suffix = utage_player_side_suffix(utage_side);
  if (info.is_utage && !side_suffix.empty()) {
    if (title.size() < side_suffix.size() ||
        title.compare(title.size() - side_suffix.size(), side_suffix.size(),
                      side_suffix) != 0) {
      title += side_suffix;
    }
  }

  return title;
}

void push_warning(std::vector<std::string> &warnings, std::string warning) {
  warnings.push_back(std::move(warning));
}

void append_dummy_tag(std::vector<std::string> &dummy_outputs,
                      std::vector<std::string> &warnings,
                      const std::string &track_id, const std::string &tag) {
  const std::size_t old_size = dummy_outputs.size();
  append_unique_string(dummy_outputs, tag);
  if (dummy_outputs.size() == old_size) {
    return;
  }
  push_warning(warnings,
               std::string(kDummyWarningPrefix) + ":" + track_id + ":" + tag);
}

std::filesystem::path
expected_track_chart_path(const std::filesystem::path &track_output,
                          ChartFormat format) {
  if (format == ChartFormat::Simai || format == ChartFormat::SimaiFes ||
      format == ChartFormat::Maidata) {
    return track_output / "maidata.txt";
  }
  return track_output / "result.ma2";
}

bool has_exported_cover_file(const std::filesystem::path &track_output) {
  static constexpr std::array<const char *, 3> kCoverNames = {
      "bg.png", "bg.jpg", "bg.jpeg"};
  return std::any_of(
      kCoverNames.begin(), kCoverNames.end(),
      [&](const char *name) { return file_non_empty(track_output / name); });
}

bool has_complete_track_output(const std::filesystem::path &track_output,
                               const AssetsOptions &options) {
  if (std::filesystem::exists(track_output) &&
      std::filesystem::is_directory(track_output)) {
    if (options.export_chart &&
        !std::filesystem::exists(
            expected_track_chart_path(track_output, options.format))) {
      return false;
    }
    if (options.export_audio && !file_non_empty(track_output / "track.mp3")) {
      return false;
    }
    if (options.export_cover && !has_exported_cover_file(track_output)) {
      return false;
    }
    if (options.export_video && !file_non_empty(track_output / "pv.mp4")) {
      return false;
    }
    return true;
  }

  if (options.export_zip) {
    auto zip_path = track_output;
    zip_path += ".zip";
    if (std::filesystem::exists(zip_path) &&
        std::filesystem::is_regular_file(zip_path)) {
      return true;
    }
  }

  return false;
}

void emit_track_output(const TrackInfo &info,
                       const std::filesystem::path &output_path,
                       bool incomplete, bool skipped_existing,
                       AssetsLogLevel log_level,
                       const std::vector<std::string> &dummy_outputs,
                       UtagePlayerSide utage_side) {
  if (log_level == AssetsLogLevel::Quiet) {
    return;
  }

  if (skipped_existing) {
    std::cout << "Skipped: ";
  } else {
    std::cout << (incomplete ? "Incomplete: " : "Completed: ");
  }
  std::cout << info.id << " " << export_display_title(info, utage_side);
  if (log_level == AssetsLogLevel::Verbose) {
    std::cout << " -> " << path_to_utf8(output_path);
  }
  if (!dummy_outputs.empty()) {
    std::cout << " [dummy: ";
    for (std::size_t i = 0; i < dummy_outputs.size(); ++i) {
      if (i != 0) {
        std::cout << ", ";
      }
      std::cout << dummy_outputs[i];
    }
    std::cout << "]";
  }
  std::cout << "\n";
  std::cout.flush();
}

void emit_log_output(const std::map<int, std::string> &compiled,
                     const std::vector<std::string> &warnings,
                     const std::filesystem::path &output, bool write_json,
                     AssetsLogLevel log_level) {
  if (!warnings.empty() && log_level != AssetsLogLevel::Verbose) {
    std::cerr << "Warnings:\n";
    for (const auto &warning : warnings) {
      std::cerr << warning << "\n";
    }
  }

  std::cout << "Total music compiled: " << compiled.size() << "\n";

  if (write_json) {
    std::string json;
    json.reserve(compiled.size() * 48);
    json += "{\n";
    std::size_t i = 0;
    for (const auto &[id, name] : compiled) {
      json += "  \"";
      json += std::to_string(id);
      json += "\": \"";
      json += json_escape(name);
      json += "\"";
      if (++i < compiled.size()) {
        json += ",";
      }
      json += "\n";
    }
    json += "}\n";
    write_text_file(output / "_index.json", json);
  }
}

void write_collections(
    const std::filesystem::path &output,
    const std::map<std::string, std::vector<std::string>> &groups) {
  for (const auto &[name, ids] : groups) {
    const auto folder =
        append_utf8_path(output / "collections",
                         sanitize_folder_name(name.empty() ? "default" : name));
    std::filesystem::create_directories(folder);
    std::string manifest;
    manifest.reserve(64 + name.size() + ids.size() * 10);
    manifest += "{\n";
    manifest += "  \"name\": \"";
    manifest += json_escape(name);
    manifest += "\",\n";
    manifest += "  \"levelIds\": [";
    for (std::size_t i = 0; i < ids.size(); ++i) {
      if (i != 0) {
        manifest += ", ";
      }
      manifest += "\"";
      manifest += ids[i];
      manifest += "\"";
    }
    manifest += "]\n";
    manifest += "}\n";
    write_text_file(folder / "manifest.json", manifest);
  }
}

bool zip_and_remove(const std::filesystem::path &folder) {
  return zip_folder_and_remove(folder);
}

std::string compose_simai_document(const TrackInfo &info,
                                   const std::map<int, std::string> &inotes,
                                   bool strict_decimal) {
  (void)info;
  (void)strict_decimal;
  std::string out;
  std::size_t reserve_size = 0;
  for (const auto &[diff, body] : inotes) {
    reserve_size += 12 + std::to_string(diff).size() + body.size();
  }
  out.reserve(reserve_size);
  for (const auto &[diff, body] : inotes) {
    out += "&inote_";
    out += std::to_string(diff);
    out += "=";
    out += body;
    out += "\n\n";
  }
  return out;
}

std::string compose_maidata_document(const TrackInfo &info,
                                     const std::map<int, std::string> &inotes,
                                     bool strict_decimal,
                                     MaidataLevelMode level_mode,
                                     UtagePlayerSide utage_side) {
  (void)strict_decimal;
  const std::string title = export_display_title(info, utage_side);
  const std::string artist = normalize_maidata_metadata_value(info.composer);
  std::string genre = normalize_maidata_metadata_value(info.genre);
  if (info.genre_id == "104") {
    genre = "ゲーム&バラエティ";
  }
  const auto completed = complete_version_fields(info.version_id, info.version);
  const std::string version_id =
      completed.first.empty() ? info.version_id : completed.first;
  const std::string version = normalize_maidata_metadata_value(
      normalize_export_version_display(completed.second));

  std::string out;
  std::size_t reserve_size = 256;
  for (const auto &[diff, body] : inotes) {
    reserve_size += 40 + body.size();
  }
  out.reserve(reserve_size);

  out += "&title=";
  out += title;
  out += "\n";
  out += "&artist=";
  out += artist;
  out += "\n";
  out += "&wholebpm=";
  out += info.bpm;
  out += "\n";
  out += "&first=0.0333\n";
  out += "&shortid=";
  out += info.short_id;
  out += "\n";
  out += "&genreid=";
  out += info.genre_id;
  out += "\n";
  if (!genre.empty() && genre != "Unknown") {
    out += "&genre=";
    out += genre;
    out += "\n";
  }
  out += "&versionid=";
  out += version_id;
  out += "\n";
  if (!version.empty() && version != "Unknown") {
    out += "&version=";
    out += version;
    out += "\n";
  }
  out += "&ChartConvertTool=MaiConv\n";
  out += "&ChartConvertToolVersion=";
  out += maiconv_build_version_string();
  out += "\n";

  for (const auto &[diff, body] : inotes) {
    const auto difficulty_meta = info.difficulties.find(diff);
    out += "&lv_";
    out += std::to_string(diff);
    out += "=";
    if (difficulty_meta != info.difficulties.end()) {
      out += (level_mode == MaidataLevelMode::Display
                  ? difficulty_meta->second.display_level
                  : difficulty_meta->second.constant_level);
    }
    out += "\n";
    out += "&des_";
    out += std::to_string(diff);
    out += "=";
    if (difficulty_meta != info.difficulties.end()) {
      out += normalize_maidata_metadata_value(difficulty_meta->second.designer);
    }
    out += "\n";
    out += "&inote_";
    out += std::to_string(diff);
    out += "=";
    out += body;
    out += "\n\n";
  }
  return out;
}

struct PhaseTiming {
  std::vector<double> samples_ms;
  double total_ms = 0.0;

  void add(std::chrono::steady_clock::duration duration) {
    const auto us =
        std::chrono::duration_cast<std::chrono::microseconds>(duration);
    const double ms = static_cast<double>(us.count()) / 1000.0;
    total_ms += ms;
    samples_ms.push_back(ms);
  }

  void merge(const PhaseTiming &other) {
    total_ms += other.total_ms;
    samples_ms.insert(samples_ms.end(), other.samples_ms.begin(),
                      other.samples_ms.end());
  }

  [[nodiscard]] double avg_ms() const {
    if (samples_ms.empty()) {
      return 0.0;
    }
    return total_ms / static_cast<double>(samples_ms.size());
  }

  [[nodiscard]] double p95_ms() const {
    if (samples_ms.empty()) {
      return 0.0;
    }
    std::vector<double> sorted = samples_ms;
    std::sort(sorted.begin(), sorted.end());
    const std::size_t idx = (sorted.size() - 1) * 95 / 100;
    return sorted[idx];
  }
};

struct AssetsTimingSummary {
  PhaseTiming source_scan;
  PhaseTiming index_build;
  PhaseTiming xml_parse;
  PhaseTiming ma2_parse_compose;
  PhaseTiming media;
  PhaseTiming write_zip;
  std::size_t metadata_cache_hits = 0;
  std::size_t metadata_cache_misses = 0;
  std::size_t asset_index_cache_hits = 0;
  std::size_t asset_index_cache_misses = 0;

  void merge(const AssetsTimingSummary &other) {
    source_scan.merge(other.source_scan);
    index_build.merge(other.index_build);
    xml_parse.merge(other.xml_parse);
    ma2_parse_compose.merge(other.ma2_parse_compose);
    media.merge(other.media);
    write_zip.merge(other.write_zip);
    metadata_cache_hits += other.metadata_cache_hits;
    metadata_cache_misses += other.metadata_cache_misses;
    asset_index_cache_hits += other.asset_index_cache_hits;
    asset_index_cache_misses += other.asset_index_cache_misses;
  }
};

void emit_timing_summary(const AssetsTimingSummary &timing) {
  const auto has_samples = [](const PhaseTiming &phase) {
    return !phase.samples_ms.empty();
  };

  if (!has_samples(timing.source_scan) && !has_samples(timing.index_build) &&
      !has_samples(timing.xml_parse) &&
      !has_samples(timing.ma2_parse_compose) && !has_samples(timing.media) &&
      !has_samples(timing.write_zip) && timing.metadata_cache_hits == 0 &&
      timing.metadata_cache_misses == 0 && timing.asset_index_cache_hits == 0 &&
      timing.asset_index_cache_misses == 0) {
    return;
  }

  const auto old_flags = std::cerr.flags();
  const auto old_precision = std::cerr.precision();
  std::cerr << std::fixed << std::setprecision(2);
  std::cerr << "Timing summary (ms):\n";

  const auto print_phase = [](const char *label, const PhaseTiming &phase) {
    if (phase.samples_ms.empty()) {
      return;
    }
    std::cerr << "  " << label << ": count=" << phase.samples_ms.size()
              << ", total=" << phase.total_ms << ", avg=" << phase.avg_ms()
              << ", p95=" << phase.p95_ms() << "\n";
  };
  print_phase("source_scan", timing.source_scan);
  print_phase("index_build", timing.index_build);
  print_phase("xml_parse", timing.xml_parse);
  print_phase("ma2_parse_compose", timing.ma2_parse_compose);
  print_phase("media", timing.media);
  print_phase("write_zip", timing.write_zip);
  if (timing.metadata_cache_hits != 0 || timing.metadata_cache_misses != 0) {
    std::cerr << "  metadata_cache: hits=" << timing.metadata_cache_hits
              << ", misses=" << timing.metadata_cache_misses << "\n";
  }
  if (timing.asset_index_cache_hits != 0 ||
      timing.asset_index_cache_misses != 0) {
    std::cerr << "  asset_index_cache: hits=" << timing.asset_index_cache_hits
              << ", misses=" << timing.asset_index_cache_misses << "\n";
  }

  std::cerr.flags(old_flags);
  std::cerr.precision(old_precision);
}

class OutputPathMutexPool {
public:
  std::shared_ptr<std::mutex> acquire(const std::filesystem::path &path) {
    const std::string key =
        lower(path_to_generic_utf8(path.lexically_normal()));
    std::lock_guard<std::mutex> guard(mutex_);
    auto &entry = locks_[key];
    if (!entry) {
      entry = std::make_shared<std::mutex>();
    }
    return entry;
  }

private:
  std::mutex mutex_;
  std::unordered_map<std::string, std::shared_ptr<std::mutex>> locks_;
};

struct TrackProcessResult {
  bool matched_music_id = false;
  bool matched_difficulty = false;
  bool matched_version = false;
  bool emit_track_output = false;
  bool incomplete = false;
  bool skipped_existing = false;
  UtagePlayerSide utage_side = UtagePlayerSide::None;
  TrackInfo info;
  std::filesystem::path final_track_output;
  std::optional<std::pair<int, std::string>> compiled_track;
  std::optional<std::pair<std::string, std::string>> collection_entry;
  std::vector<std::string> warnings;
  std::vector<std::string> dummy_outputs;
  std::optional<std::string> fatal_error;
  AssetsTimingSummary timing;
};

TrackProcessResult process_track_folder(
    const std::filesystem::path &folder, UtagePlayerSide forced_utage_side,
    const AssetsOptions &options, const NumericFilterSet &target_music_filters,
    const NumericFilterSet &target_difficulty_filters,
    const VersionFilterSet &target_version_filters,
    const std::vector<std::filesystem::path> &music_bases,
    const std::vector<std::filesystem::path> &cover_bases,
    const std::vector<std::filesystem::path> &video_bases,
    const std::vector<AssetIndex> &music_indexes,
    const std::vector<AssetIndex> &cover_indexes,
    const std::vector<AssetIndex> &video_indexes,
    OutputPathMutexPool &output_path_mutex_pool) {
  TrackProcessResult result;
  result.utage_side = forced_utage_side;
  try {
    const std::string folder_name = path_to_utf8(folder.filename());
    std::string fallback_id;
    if (folder_name.rfind("music", 0) == 0) {
      fallback_id = folder_name.substr(5);
    } else {
      fallback_id = folder_name;
    }
    fallback_id = pad_music_id(fallback_id, 6);

    const auto xml_path = folder / "Music.xml";
    TrackInfo info;
    if (std::filesystem::exists(xml_path)) {
      const auto xml_begin = std::chrono::steady_clock::now();
      bool cache_hit = false;
      info = parse_track_info_cached(xml_path, fallback_id, &cache_hit);
      result.timing.xml_parse.add(std::chrono::steady_clock::now() - xml_begin);
      if (cache_hit) {
        ++result.timing.metadata_cache_hits;
      } else {
        ++result.timing.metadata_cache_misses;
      }
    } else {
      info = default_track_info(fallback_id);
    }

    if (!info.is_utage) {
      result.utage_side = UtagePlayerSide::None;
    }

    if (is_reserved_music_id(info.id)) {
      return result;
    }

    if (target_music_filters.active() &&
        !target_music_filters.matches(info.id)) {
      return result;
    }
    if (target_music_filters.active()) {
      result.matched_music_id = true;
    }
    if (target_version_filters.active() &&
        !matches_version_filter(info, target_version_filters)) {
      return result;
    }
    if (target_version_filters.active()) {
      result.matched_version = true;
    }

    std::vector<std::filesystem::path> all_ma2_files;
    for (const auto &entry : std::filesystem::directory_iterator(folder)) {
      if (!entry.is_regular_file() ||
          lower(path_to_utf8(entry.path().extension())) != ".ma2") {
        continue;
      }
      all_ma2_files.push_back(entry.path());
    }
    std::sort(all_ma2_files.begin(), all_ma2_files.end());

    bool zero_based_difficulty = info.zero_based_difficulty;
    if (!zero_based_difficulty) {
      for (const auto &ma2_file : all_ma2_files) {
        const auto parsed = parse_ma2_difficulty(ma2_file);
        if (parsed.has_value() && *parsed == 0) {
          zero_based_difficulty = true;
          break;
        }
      }
    }

    struct SelectedChart {
      std::filesystem::path ma2_file;
      int output_difficulty = 1;
      UtagePlayerSide utage_side = UtagePlayerSide::None;
    };

    std::vector<SelectedChart> selected_charts;
    std::set<std::string> selected_chart_keys;
    const auto append_selected_chart =
        [&](const std::filesystem::path &ma2_file, int output_difficulty) {
          const std::string dedupe_key =
              lower(path_to_generic_utf8(ma2_file.lexically_normal())) + "|" +
              std::to_string(output_difficulty);
          if (!selected_chart_keys.insert(dedupe_key).second) {
            return;
          }
          selected_charts.push_back(
              {ma2_file, output_difficulty,
               detect_utage_player_side_from_chart(ma2_file)});
        };

    if (!info.chart_output_order.empty()) {
      std::map<std::string, std::filesystem::path> ma2_by_key;
      for (const auto &ma2_file : all_ma2_files) {
        ma2_by_key[normalize_chart_path_key(ma2_file)] = ma2_file;
      }

      for (const auto &chart_entry : info.chart_output_order) {
        std::vector<std::filesystem::path> resolved_paths;

        auto found = ma2_by_key.find(chart_entry.chart_file_key);
        if (found != ma2_by_key.end()) {
          resolved_paths.push_back(found->second);
        } else {
          const std::string &name = chart_entry.chart_file_key;
          if (name.size() > 4 && name.substr(name.size() - 4) == ".ma2") {
            const std::string base_name = name.substr(0, name.size() - 4);
            const std::string fallback_l_name = base_name + "_l.ma2";
            const std::string fallback_r_name = base_name + "_r.ma2";

            const auto found_l = ma2_by_key.find(fallback_l_name);
            if (found_l != ma2_by_key.end()) {
              resolved_paths.push_back(found_l->second);
            }
            const auto found_r = ma2_by_key.find(fallback_r_name);
            if (found_r != ma2_by_key.end()) {
              resolved_paths.push_back(found_r->second);
            }
          }
        }

        if (resolved_paths.empty()) {
          continue;
        }

        if (target_difficulty_filters.active() &&
            !target_difficulty_filters.matches(
                std::to_string(chart_entry.output_difficulty))) {
          continue;
        }

        for (const auto &resolved_path : resolved_paths) {
          append_selected_chart(resolved_path, chart_entry.output_difficulty);
        }
      }
    }

    if (selected_charts.empty()) {
      for (const auto &ma2_file : all_ma2_files) {
        const int inferred_difficulty =
            infer_output_difficulty(info, ma2_file, zero_based_difficulty);
        if (target_difficulty_filters.active() &&
            !target_difficulty_filters.matches(
                std::to_string(inferred_difficulty))) {
          continue;
        }
        append_selected_chart(ma2_file, inferred_difficulty);
      }
    }

    if (forced_utage_side != UtagePlayerSide::None) {
      std::vector<SelectedChart> filtered_charts;
      filtered_charts.reserve(selected_charts.size());
      for (const auto &chart : selected_charts) {
        if (chart.utage_side == UtagePlayerSide::None ||
            chart.utage_side == forced_utage_side) {
          filtered_charts.push_back(chart);
        }
      }
      selected_charts = std::move(filtered_charts);
    }

    if (selected_charts.empty() && target_difficulty_filters.active()) {
      return result;
    }
    if (target_difficulty_filters.active()) {
      result.matched_difficulty = true;
    }

    const std::string category =
        category_name_for_layout(info, options.export_layout);
    std::filesystem::path category_folder = options.output_path;
    if (!category.empty()) {
      category_folder =
          append_utf8_path(category_folder, sanitize_folder_name(category));
    }

    const std::string display_name =
        export_display_title(info, forced_utage_side);
    const std::string utage_side_suffix =
        info.is_utage ? utage_player_side_suffix(forced_utage_side) : "";
    const auto id_only_track_output = [&]() {
      return append_utf8_path(category_folder, info.id + utage_side_suffix);
    };
    std::filesystem::path track_output;
    bool used_id_folder_fallback = false;
    if (options.music_id_folder_name) {
      track_output = id_only_track_output();
    } else {
      const std::string folder_stem =
          info.id + "_" + sanitize_folder_name(display_name);
      try {
        track_output = append_utf8_path(category_folder, folder_stem);
      } catch (const std::exception &) {
        track_output = id_only_track_output();
        used_id_folder_fallback = true;
      }
    }

    auto output_path_mutex = output_path_mutex_pool.acquire(track_output);
    std::unique_lock<std::mutex> output_guard(*output_path_mutex);

    if (options.skip_existing_exports) {
      std::optional<std::filesystem::path> existing_complete_output;
      if (has_complete_track_output(track_output, options)) {
        existing_complete_output = track_output;
      } else if (!options.music_id_folder_name) {
        const auto fallback_track_output = id_only_track_output();
        if (fallback_track_output != track_output &&
            has_complete_track_output(fallback_track_output, options)) {
          existing_complete_output = fallback_track_output;
        }
      }

      if (existing_complete_output.has_value()) {
        result.info = std::move(info);
        result.final_track_output = *existing_complete_output;
        result.skipped_existing = true;
        result.emit_track_output = true;
        return result;
      }
    }

    std::filesystem::create_directories(category_folder);
    try {
      std::filesystem::create_directories(track_output);
    } catch (const std::filesystem::filesystem_error &) {
      if (options.music_id_folder_name) {
        throw;
      }
      const auto fallback_track_output = id_only_track_output();
      if (fallback_track_output != track_output) {
        // If output path falls back, lock the final path to keep writes
        // serialized even when multiple workers hit fallback at once.
        output_guard.unlock();
        track_output = fallback_track_output;
        output_path_mutex = output_path_mutex_pool.acquire(track_output);
        output_guard = std::unique_lock<std::mutex>(*output_path_mutex);
      } else {
        track_output = fallback_track_output;
      }
      std::filesystem::create_directories(track_output);
      used_id_folder_fallback = true;
    }
    if (used_id_folder_fallback) {
      push_warning(result.warnings,
                   "Folder name fallback to id for track " + info.id +
                       ": unsupported characters in export title");
    }
    std::filesystem::path final_track_output = track_output;
    const bool export_chart = options.export_chart;
    const bool export_audio = options.export_audio;
    const bool export_cover = options.export_cover;
    const bool export_video = options.export_video;

    Ma2Tokenizer tokenizer;
    Ma2Parser parser;
    Ma2Composer ma2_composer;
    simai::Compiler simai_composer;

    std::map<int, std::string> inotes;
    double longest_chart_seconds = 0.0;
    std::chrono::steady_clock::duration ma2_duration{};
    std::chrono::steady_clock::duration write_duration{};
    for (const auto &selected_chart : selected_charts) {
      const auto &ma2_file = selected_chart.ma2_file;
      const int output_difficulty = selected_chart.output_difficulty;
      const auto ma2_begin = std::chrono::steady_clock::now();
      const auto chart = parser.parse(tokenizer.tokenize_file(ma2_file));
      Chart transformed = chart;
      if (options.rotate.has_value()) {
        transformed.rotate(*options.rotate);
      }
      if (options.shift_ticks != 0) {
        transformed.shift_by_offset(options.shift_ticks);
      }
      longest_chart_seconds = std::max(
          longest_chart_seconds, estimate_chart_duration_seconds(transformed));

      if (!export_chart) {
        ma2_duration += std::chrono::steady_clock::now() - ma2_begin;
        continue;
      }

      if (options.format == ChartFormat::Simai ||
          options.format == ChartFormat::SimaiFes ||
          options.format == ChartFormat::Maidata) {
        if (info.is_utage && inotes.find(output_difficulty) != inotes.end()) {
          ma2_duration += std::chrono::steady_clock::now() - ma2_begin;
          continue;
        }
        inotes[output_difficulty] = simai_composer.compile_chart(transformed);
        ma2_duration += std::chrono::steady_clock::now() - ma2_begin;
      } else {
        const std::string text =
            ma2_composer.compose(transformed, options.format);
        ma2_duration += std::chrono::steady_clock::now() - ma2_begin;
        const auto write_begin = std::chrono::steady_clock::now();
        write_text_file(track_output / "result.ma2", text);
        write_duration += std::chrono::steady_clock::now() - write_begin;
      }
    }
    result.timing.ma2_parse_compose.add(ma2_duration);

    if (export_chart &&
        (options.format == ChartFormat::Maidata || !inotes.empty())) {
      const auto write_begin = std::chrono::steady_clock::now();
      const std::string payload =
          (options.format == ChartFormat::Maidata)
              ? compose_maidata_document(info, inotes, options.strict_decimal,
                                         options.maidata_level_mode,
                                         forced_utage_side)
              : compose_simai_document(info, inotes, options.strict_decimal);
      write_text_file(track_output / "maidata.txt", payload);
      write_duration += std::chrono::steady_clock::now() - write_begin;
    }
    if (write_duration != std::chrono::steady_clock::duration::zero()) {
      result.timing.write_zip.add(write_duration);
    }

    bool audio_incomplete = false;
    bool cover_incomplete = false;
    bool video_incomplete = false;

    int id_number = to_int(info.id, 0);
    if (id_number < 0) {
      id_number = -id_number;
    }
    const std::string non_dx_id =
        pad_music_id(std::to_string(id_number % 10000), 6);
    const std::string short_id =
        non_dx_id.size() >= 2 ? non_dx_id.substr(2) : non_dx_id;
    const std::string cue_id =
        info.cue_id.empty() ? non_dx_id : pad_music_id(info.cue_id, 6);
    const std::string cue_short_id =
        cue_id.size() >= 2 ? cue_id.substr(2) : cue_id;
    const std::string movie_id =
        info.movie_id.empty() ? non_dx_id : pad_music_id(info.movie_id, 6);
    const std::string movie_short_id =
        movie_id.size() >= 2 ? movie_id.substr(2) : movie_id;

    const auto media_begin = std::chrono::steady_clock::now();
    std::vector<std::string> stems;
    std::vector<std::string> primary_candidates;
    std::vector<std::string> secondary_candidates;
    std::vector<std::string> lookup_names;

    if (export_audio && !music_bases.empty()) {
      stems.clear();
      stems.reserve(6);
      append_unique_string(stems, "music" + info.id);
      append_unique_string(stems, "music" + non_dx_id);
      append_unique_string(stems, "music00" + short_id);
      append_unique_string(stems, "music" + cue_id);
      append_unique_string(stems, "music00" + cue_short_id);

      append_suffix_candidates(stems, {".mp3", ".ogg"}, primary_candidates);
      append_suffix_candidates(stems, {".acb", ".awb"}, secondary_candidates);
      lookup_names.clear();
      lookup_names.reserve(primary_candidates.size() +
                           secondary_candidates.size());
      lookup_names.insert(lookup_names.end(), primary_candidates.begin(),
                          primary_candidates.end());
      lookup_names.insert(lookup_names.end(), secondary_candidates.begin(),
                          secondary_candidates.end());

      const auto found_audio =
          find_asset_candidates_in_indexes(music_indexes, lookup_names);
      const auto compressed_audio =
          first_found_candidate_in_order(found_audio, primary_candidates);
      if (!compressed_audio.empty()) {
        const std::string ext =
            lower(path_to_utf8(compressed_audio.extension()));
        if (ext == ".mp3") {
          std::filesystem::copy_file(
              compressed_audio, track_output / "track.mp3",
              std::filesystem::copy_options::overwrite_existing);
        } else {
          if (!convert_audio_to_mp3(compressed_audio,
                                    track_output / "track.mp3")) {
            push_warning(
                result.warnings,
                "Audio conversion failed: " + path_to_utf8(compressed_audio) +
                    " -> " + path_to_utf8(track_output / "track.mp3"));
            audio_incomplete = true;
          }
        }
      } else {
        std::filesystem::path acb_source;
        std::filesystem::path awb_source;
        for (const auto &base_name : stems) {
          const auto acb_it = found_audio.find(lower(base_name + ".acb"));
          const auto awb_it = found_audio.find(lower(base_name + ".awb"));
          if (acb_it != found_audio.end() && awb_it != found_audio.end()) {
            acb_source = acb_it->second;
            awb_source = awb_it->second;
            break;
          }
        }
        if (!acb_source.empty() && !awb_source.empty()) {
          if (!convert_acb_awb_to_mp3(acb_source, awb_source,
                                      track_output / "track.mp3")) {
            push_warning(
                result.warnings,
                "Audio conversion failed: " + path_to_utf8(acb_source) + " + " +
                    path_to_utf8(awb_source) + " -> " +
                    path_to_utf8(track_output / "track.mp3"));
            audio_incomplete = true;
          }
        } else {
          push_warning(result.warnings,
                       "Music missing: " + info.name + " (" + info.id + ")");
          audio_incomplete = true;
        }
      }
    }
    if (export_cover && !cover_bases.empty()) {
      stems.clear();
      stems.reserve(8);
      append_unique_string(stems, "UI_Jacket_" + info.id);
      append_unique_string(stems, "UI_Jacket_00" + short_id);
      append_unique_string(stems, "ui_jacket_" + info.id);
      append_unique_string(stems, "ui_jacket_" + non_dx_id);
      append_unique_string(stems, "ui_jacket_" + info.id + "_s");
      append_unique_string(stems, "ui_jacket_" + non_dx_id + "_s");
      append_unique_string(stems, "UI_Jacket_" + movie_id);
      append_unique_string(stems, "UI_Jacket_00" + movie_short_id);
      append_unique_string(stems, "ui_jacket_" + movie_id);
      append_unique_string(stems, "ui_jacket_00" + movie_short_id);
      append_unique_string(stems, "ui_jacket_" + movie_id + "_s");
      append_unique_string(stems, "ui_jacket_00" + movie_short_id + "_s");

      append_suffix_candidates(stems, {".png", ".jpg", ".jpeg"},
                               primary_candidates);
      append_prefixed_suffix_candidates(
          stems, "jacket/", {".png", ".jpg", ".jpeg"}, secondary_candidates);
      std::vector<std::string> image_names;
      image_names.reserve(primary_candidates.size() +
                          secondary_candidates.size() + (stems.size() * 3));
      image_names.insert(image_names.end(), primary_candidates.begin(),
                         primary_candidates.end());
      image_names.insert(image_names.end(), secondary_candidates.begin(),
                         secondary_candidates.end());
      append_prefixed_suffix_candidates(
          stems, "jacket_s/", {".png", ".jpg", ".jpeg"}, secondary_candidates);
      image_names.insert(image_names.end(), secondary_candidates.begin(),
                         secondary_candidates.end());

      append_suffix_candidates(stems, {".ab"}, primary_candidates);
      append_prefixed_suffix_candidates(stems, "jacket/", {".ab"},
                                        secondary_candidates);
      std::vector<std::string> ab_names;
      ab_names.reserve(primary_candidates.size() + secondary_candidates.size() +
                       stems.size());
      ab_names.insert(ab_names.end(), primary_candidates.begin(),
                      primary_candidates.end());
      ab_names.insert(ab_names.end(), secondary_candidates.begin(),
                      secondary_candidates.end());
      append_prefixed_suffix_candidates(stems, "jacket_s/", {".ab"},
                                        secondary_candidates);
      ab_names.insert(ab_names.end(), secondary_candidates.begin(),
                      secondary_candidates.end());

      lookup_names.clear();
      lookup_names.reserve(image_names.size() + ab_names.size());
      lookup_names.insert(lookup_names.end(), image_names.begin(),
                          image_names.end());
      lookup_names.insert(lookup_names.end(), ab_names.begin(), ab_names.end());
      const auto found_cover =
          find_asset_candidates_in_indexes(cover_indexes, lookup_names);

      const auto cover_image =
          first_found_candidate_in_order(found_cover, image_names);
      if (!cover_image.empty()) {
        std::string ext = lower(path_to_utf8(cover_image.extension()));
        if (ext.empty()) {
          ext = ".png";
        }
        std::filesystem::copy_file(
            cover_image, track_output / ("bg" + ext),
            std::filesystem::copy_options::overwrite_existing);
      } else {
        bool cover_exported = false;
        std::string last_failed_cover_ab;
        for (const auto &ab_name : ab_names) {
          const auto it = found_cover.find(ab_name);
          if (it == found_cover.end() || it->second.empty()) {
            continue;
          }

          const auto &cover_ab = it->second;
          if (const auto pseudo_image_ext =
                  detect_image_extension_by_magic(cover_ab);
              pseudo_image_ext.has_value()) {
            std::filesystem::copy_file(
                cover_ab, track_output / ("bg" + *pseudo_image_ext),
                std::filesystem::copy_options::overwrite_existing);
            cover_exported = true;
            break;
          }

          if (convert_ab_to_png(cover_ab, track_output / "bg.png")) {
            cover_exported = true;
            break;
          }
          last_failed_cover_ab = path_to_utf8(cover_ab);
        }

        if (!cover_exported) {
          if (!last_failed_cover_ab.empty()) {
            push_warning(result.warnings,
                         "Cover conversion failed: " + last_failed_cover_ab +
                             " -> " + path_to_utf8(track_output / "bg.png"));
          } else {
            push_warning(result.warnings,
                         "Cover missing: " + info.name + " (" + info.id + ")");
          }
          cover_incomplete = true;
        }
      }
    }
    if (export_video && !video_bases.empty()) {
      stems.clear();
      stems.reserve(8);
      append_unique_string(stems, info.id);
      append_unique_string(stems, non_dx_id);
      append_unique_string(stems, "00" + short_id);
      append_unique_string(stems, short_id);
      append_unique_string(stems, movie_id);
      append_unique_string(stems, "00" + movie_short_id);
      append_unique_string(stems, movie_short_id);

      append_suffix_candidates(stems, {".mp4"}, primary_candidates);
      const std::vector<std::string> video_mp4_names = primary_candidates;
      append_suffix_candidates(stems, {".dat"}, primary_candidates);
      const std::vector<std::string> video_dat_names = primary_candidates;
      append_suffix_candidates(stems, {".usm"}, primary_candidates);
      const std::vector<std::string> video_usm_names = primary_candidates;
      append_suffix_candidates(stems, {".crid"}, primary_candidates);
      const std::vector<std::string> video_crid_names = primary_candidates;

      lookup_names.clear();
      lookup_names.reserve(video_mp4_names.size() + video_dat_names.size() +
                           video_usm_names.size() + video_crid_names.size());
      lookup_names.insert(lookup_names.end(), video_mp4_names.begin(),
                          video_mp4_names.end());
      lookup_names.insert(lookup_names.end(), video_dat_names.begin(),
                          video_dat_names.end());
      lookup_names.insert(lookup_names.end(), video_usm_names.begin(),
                          video_usm_names.end());
      lookup_names.insert(lookup_names.end(), video_crid_names.begin(),
                          video_crid_names.end());
      const auto found_video =
          find_asset_candidates_in_indexes(video_indexes, lookup_names);

      auto video_source =
          first_found_candidate_in_order(found_video, video_mp4_names);
      if (!video_source.empty()) {
        std::filesystem::copy_file(
            video_source, track_output / "pv.mp4",
            std::filesystem::copy_options::overwrite_existing);
      } else {
        video_source =
            first_found_candidate_in_order(found_video, video_dat_names);
        if (video_source.empty()) {
          video_source =
              first_found_candidate_in_order(found_video, video_usm_names);
        }
        if (video_source.empty()) {
          video_source =
              first_found_candidate_in_order(found_video, video_crid_names);
        }

        if (video_source.empty()) {
          if (info.movie_debug_placeholder) {
            push_warning(result.warnings,
                         "Video missing (debug movie placeholder): " +
                             info.name + " (" + info.id + ")");
          } else {
            push_warning(result.warnings,
                         "Video missing: " + info.name + " (" + info.id + ")");
            video_incomplete = true;
          }
        } else {
          if (!convert_dat_or_usm_to_mp4(video_source,
                                         track_output / "pv.mp4")) {
            push_warning(
                result.warnings,
                "Video conversion failed: " + path_to_utf8(video_source) +
                    " -> " + path_to_utf8(track_output / "pv.mp4"));
            video_incomplete = true;
          }
        }
      }
    }

    if (options.dummy_assets && (export_audio || export_video)) {
      if (export_audio) {
        const auto track_mp3 = track_output / "track.mp3";
        if (!file_non_empty(track_mp3)) {
          const double dummy_duration_seconds =
              std::max(1.0, longest_chart_seconds);
          if (generate_silent_mp3(track_mp3, dummy_duration_seconds)) {
            audio_incomplete = false;
            append_dummy_tag(result.dummy_outputs, result.warnings, info.id,
                             kDummyTagMissingAudio);
          } else {
            push_warning(result.warnings, "Dummy audio generation failed: " +
                                              path_to_utf8(track_mp3));
            audio_incomplete = true;
          }
        }
      }

      if (export_video) {
        const auto pv_mp4 = track_output / "pv.mp4";
        if (!file_non_empty(pv_mp4)) {
          const auto bg_png = track_output / "bg.png";
          bool dummy_video_ok = false;
          if (file_non_empty(bg_png)) {
            dummy_video_ok =
                generate_single_frame_mp4_from_image(bg_png, pv_mp4);
            if (dummy_video_ok) {
              append_dummy_tag(result.dummy_outputs, result.warnings, info.id,
                               kDummyTagMissingVideo);
              append_dummy_tag(result.dummy_outputs, result.warnings, info.id,
                               kDummyTagSourceBgPng);
            }
          } else {
            dummy_video_ok = generate_single_frame_black_mp4(pv_mp4);
            if (dummy_video_ok) {
              append_dummy_tag(result.dummy_outputs, result.warnings, info.id,
                               kDummyTagMissingVideo);
              append_dummy_tag(result.dummy_outputs, result.warnings, info.id,
                               kDummyTagBlackFrame);
            }
          }

          if (dummy_video_ok) {
            video_incomplete = false;
          } else {
            push_warning(result.warnings, "Dummy video generation failed: " +
                                              path_to_utf8(pv_mp4));
            video_incomplete = true;
          }
        }
      }
    }

    const bool incomplete = (export_audio && audio_incomplete) ||
                            (export_cover && cover_incomplete) ||
                            (export_video && video_incomplete);
    result.timing.media.add(std::chrono::steady_clock::now() - media_begin);

    if (incomplete) {
      auto incomplete_path = track_output;
      incomplete_path += "_Incomplete";
      if (std::filesystem::exists(incomplete_path)) {
        std::filesystem::remove_all(incomplete_path);
      }
      std::filesystem::rename(track_output, incomplete_path);
      final_track_output = incomplete_path;
    }

    if (incomplete && !options.ignore_incomplete_assets) {
      // Keep per-track progress visible even when the run eventually fails due
      // to incomplete assets.
      result.info = info;
      result.final_track_output = final_track_output;
      result.incomplete = true;
      result.emit_track_output = true;
      result.fatal_error = "Incomplete assets found. Use --ignore to continue.";
      return result;
    }

    if (!incomplete) {
      result.compiled_track = std::make_pair(to_int(info.id), info.name);
      const std::string collection_name =
          category.empty() ? "default" : category;
      result.collection_entry = std::make_pair(collection_name, info.id);

      if (options.export_zip) {
        const auto zip_begin = std::chrono::steady_clock::now();
        if (!zip_and_remove(track_output)) {
          push_warning(result.warnings,
                       "Zip export failed: " + path_to_utf8(track_output));
        }
        result.timing.write_zip.add(std::chrono::steady_clock::now() -
                                    zip_begin);
      }
    }

    result.info = std::move(info);
    result.final_track_output = std::move(final_track_output);
    result.incomplete = incomplete;
    result.emit_track_output = true;
    return result;
  } catch (const std::exception &ex) {
    result.fatal_error = ex.what();
    return result;
  } catch (...) {
    result.fatal_error = "Unknown error while processing track";
    return result;
  }
}

} // namespace

int run_compile_assets(const AssetsOptions &options) {
  try {
    if (options.streaming_assets_path.empty() || options.output_path.empty()) {
      throw std::runtime_error("input path and output path are required");
    }
    if (!std::filesystem::exists(options.streaming_assets_path)) {
      throw std::runtime_error("Input folder not found: " +
                               path_to_utf8(options.streaming_assets_path));
    }
    if (options.jobs < 1) {
      throw std::runtime_error("jobs must be >= 1");
    }
    if (!options.export_chart && !options.export_audio &&
        !options.export_cover && !options.export_video) {
      throw std::runtime_error("at least one export target must be enabled");
    }

    std::filesystem::create_directories(options.output_path);
    const bool timing_enabled =
        options.enable_timing || options.log_level == AssetsLogLevel::Verbose;

    NumericFilterSet target_music_filters = compile_music_id_filters(options);
    std::vector<std::string> reserved_music_ids;
    for (auto it = target_music_filters.exact.begin();
         it != target_music_filters.exact.end();) {
      if (is_reserved_music_id(*it)) {
        reserved_music_ids.push_back(*it);
        it = target_music_filters.exact.erase(it);
      } else {
        ++it;
      }
    }
    for (const auto &reserved_id : reserved_music_ids) {
      std::cout << "Skipping reserved music id: " << reserved_id << "\n";
    }
    if (target_music_filters.provided && !target_music_filters.active()) {
      return kSuccess;
    }

    NumericFilterSet target_difficulty_filters =
        compile_difficulty_filters(options);
    if (target_difficulty_filters.provided && !target_music_filters.provided) {
      throw std::runtime_error("difficulty requires music id");
    }
    VersionFilterSet target_version_filters = compile_version_filters(options);

    bool matched_music_id = false;
    bool matched_difficulty = false;
    bool matched_version = false;

    std::map<int, std::string> compiled_tracks;
    std::vector<std::string> warnings;
    std::map<std::string, std::vector<std::string>> collections;
    std::set<std::string> collection_seen;
    AssetsTimingSummary timing;

    const auto source_scan_begin = std::chrono::steady_clock::now();
    std::vector<std::filesystem::path> source_roots;
    source_roots.push_back(options.streaming_assets_path);
    for (const auto &entry :
         std::filesystem::directory_iterator(options.streaming_assets_path)) {
      if (entry.is_directory()) {
        source_roots.push_back(entry.path());
      }
    }
    std::sort(source_roots.begin(), source_roots.end());
    source_roots.erase(std::unique(source_roots.begin(), source_roots.end()),
                       source_roots.end());

    std::vector<std::filesystem::path> music_bases;
    std::vector<std::filesystem::path> cover_bases;
    std::vector<std::filesystem::path> video_bases;

    if (options.export_audio) {
      if (options.music_path.has_value()) {
        music_bases.push_back(*options.music_path);
      } else {
        music_bases = detect_asset_bases(source_roots, "SoundData");
      }
    }

    if (options.export_cover) {
      if (options.cover_path.has_value()) {
        cover_bases.push_back(*options.cover_path);
      } else {
        cover_bases = detect_asset_bases(source_roots, "AssetBundleImages");
      }
    }

    if (options.export_video) {
      if (options.video_path.has_value()) {
        video_bases.push_back(*options.video_path);
      } else {
        video_bases = detect_asset_bases(source_roots, "MovieData");
      }
    }
    timing.source_scan.add(std::chrono::steady_clock::now() -
                           source_scan_begin);

    const auto index_build_begin = std::chrono::steady_clock::now();
    const std::filesystem::path index_cache_root =
        std::filesystem::temp_directory_path() / "maiconv_asset_indexes_v1";
    std::size_t music_index_hits = 0;
    std::size_t music_index_misses = 0;
    const auto music_indexes = build_asset_indexes_cached(
        music_bases, index_cache_root, &music_index_hits, &music_index_misses);
    std::size_t cover_index_hits = 0;
    std::size_t cover_index_misses = 0;
    const auto cover_indexes = build_asset_indexes_cached(
        cover_bases, index_cache_root, &cover_index_hits, &cover_index_misses);
    std::size_t video_index_hits = 0;
    std::size_t video_index_misses = 0;
    const auto video_indexes = build_asset_indexes_cached(
        video_bases, index_cache_root, &video_index_hits, &video_index_misses);
    timing.asset_index_cache_hits =
        music_index_hits + cover_index_hits + video_index_hits;
    timing.asset_index_cache_misses =
        music_index_misses + cover_index_misses + video_index_misses;
    timing.index_build.add(std::chrono::steady_clock::now() -
                           index_build_begin);

    struct FolderPick {
      std::filesystem::path folder;
      int root_priority = -1;
      std::string tie_break_key;
    };
    std::unordered_map<std::string, FolderPick> folder_by_music_id;
    for (const auto &root : source_roots) {
      const auto music_root = root / "music";
      if (!std::filesystem::exists(music_root) ||
          !std::filesystem::is_directory(music_root)) {
        continue;
      }
      const int root_priority = source_root_numeric_priority(root);
      for (const auto &entry :
           std::filesystem::directory_iterator(music_root)) {
        if (!entry.is_directory()) {
          continue;
        }

        const std::filesystem::path folder = entry.path();
        const std::string id_key = music_folder_id_key(folder);
        const std::string tie_break_key = root_pick_key(folder);

        auto it = folder_by_music_id.find(id_key);
        if (it == folder_by_music_id.end()) {
          folder_by_music_id.emplace(
              id_key, FolderPick{folder, root_priority, tie_break_key});
          continue;
        }

        if (is_more_preferred(root_priority, tie_break_key,
                              it->second.root_priority,
                              it->second.tie_break_key)) {
          it->second = FolderPick{folder, root_priority, tie_break_key};
        }
      }
    }

    std::vector<std::filesystem::path> folders;
    folders.reserve(folder_by_music_id.size());
    for (const auto &[_, pick] : folder_by_music_id) {
      folders.push_back(pick.folder);
    }
    std::sort(folders.begin(), folders.end());
    if (folders.empty()) {
      throw std::runtime_error("No music folders found under input path: " +
                               path_to_utf8(options.streaming_assets_path));
    }

    struct TrackFolderJob {
      std::filesystem::path folder;
      UtagePlayerSide utage_side = UtagePlayerSide::None;
    };

    std::vector<TrackFolderJob> jobs;
    jobs.reserve(folders.size() * 2);
    for (const auto &folder : folders) {
      bool has_left = false;
      bool has_right = false;
      for (const auto &entry : std::filesystem::directory_iterator(folder)) {
        if (!entry.is_regular_file() ||
            lower(path_to_utf8(entry.path().extension())) != ".ma2") {
          continue;
        }

        const UtagePlayerSide side =
            detect_utage_player_side_from_chart(entry.path());
        has_left = has_left || side == UtagePlayerSide::Left;
        has_right = has_right || side == UtagePlayerSide::Right;
        if (has_left && has_right) {
          break;
        }
      }

      bool split_lr_jobs = false;
      if (has_left && has_right) {
        const auto xml_path = folder / "Music.xml";
        if (std::filesystem::exists(xml_path)) {
          bool cache_hit = false;
          const TrackInfo parsed = parse_track_info_cached(
              xml_path, music_folder_id_key(folder), &cache_hit);
          split_lr_jobs = parsed.is_utage;
        }
      }

      if (split_lr_jobs) {
        jobs.push_back({folder, UtagePlayerSide::Left});
        jobs.push_back({folder, UtagePlayerSide::Right});
      } else {
        jobs.push_back({folder, UtagePlayerSide::None});
      }
    }

    std::sort(jobs.begin(), jobs.end(),
              [](const TrackFolderJob &lhs, const TrackFolderJob &rhs) {
                const std::string lhs_key =
                    lower(path_to_generic_utf8(lhs.folder.lexically_normal()));
                const std::string rhs_key =
                    lower(path_to_generic_utf8(rhs.folder.lexically_normal()));
                if (lhs_key != rhs_key) {
                  return lhs_key < rhs_key;
                }
                return static_cast<int>(lhs.utage_side) <
                       static_cast<int>(rhs.utage_side);
              });

    const std::size_t worker_count =
        std::min(jobs.size(), static_cast<std::size_t>(options.jobs));
    std::vector<TrackProcessResult> results(jobs.size());
    OutputPathMutexPool output_path_mutex_pool;
    std::mutex progress_output_mutex;
    const auto emit_track_progress_immediately =
        [&](const TrackProcessResult &result) {
          if (!result.emit_track_output) {
            return;
          }
          std::lock_guard<std::mutex> progress_output_guard(
              progress_output_mutex);
          emit_track_output(result.info, result.final_track_output,
                            result.incomplete, result.skipped_existing,
                            options.log_level, result.dummy_outputs,
                            result.utage_side);
        };

    if (worker_count <= 1) {
      for (std::size_t i = 0; i < jobs.size(); ++i) {
        results[i] = process_track_folder(
            jobs[i].folder, jobs[i].utage_side, options, target_music_filters,
            target_difficulty_filters, target_version_filters, music_bases,
            cover_bases, video_bases, music_indexes, cover_indexes,
            video_indexes, output_path_mutex_pool);
        emit_track_progress_immediately(results[i]);
        if (results[i].fatal_error.has_value()) {
          break;
        }
      }
    } else {
      std::atomic<std::size_t> next_index{0};
      std::atomic<bool> stop_processing{false};
      std::vector<std::thread> workers;
      workers.reserve(worker_count);
      for (std::size_t i = 0; i < worker_count; ++i) {
        workers.emplace_back([&]() {
          while (true) {
            if (stop_processing.load(std::memory_order_relaxed)) {
              return;
            }
            const std::size_t index =
                next_index.fetch_add(1, std::memory_order_relaxed);
            if (index >= jobs.size()) {
              return;
            }
            if (stop_processing.load(std::memory_order_relaxed)) {
              return;
            }
            results[index] = process_track_folder(
                jobs[index].folder, jobs[index].utage_side, options,
                target_music_filters, target_difficulty_filters,
                target_version_filters, music_bases, cover_bases, video_bases,
                music_indexes, cover_indexes, video_indexes,
                output_path_mutex_pool);
            emit_track_progress_immediately(results[index]);
            if (results[index].fatal_error.has_value()) {
              stop_processing.store(true, std::memory_order_relaxed);
            }
          }
        });
      }
      for (auto &worker : workers) {
        worker.join();
      }
    }

    std::vector<std::string> verbose_warning_buffer;
    std::size_t tracks_since_warning_flush = 0;
    auto last_warning_flush = std::chrono::steady_clock::now();
    constexpr std::size_t kVerboseWarningFlushTracks = 8;
    constexpr auto kVerboseWarningFlushInterval =
        std::chrono::milliseconds(500);
    const auto flush_verbose_warnings = [&](bool force) {
      if (options.log_level != AssetsLogLevel::Verbose ||
          verbose_warning_buffer.empty()) {
        return;
      }
      const auto now = std::chrono::steady_clock::now();
      if (!force && tracks_since_warning_flush < kVerboseWarningFlushTracks &&
          now - last_warning_flush < kVerboseWarningFlushInterval) {
        return;
      }

      for (const auto &warning : verbose_warning_buffer) {
        std::cerr << "Warning: " << warning << "\n";
      }
      verbose_warning_buffer.clear();
      tracks_since_warning_flush = 0;
      last_warning_flush = now;
    };

    std::optional<std::string> first_fatal_error;
    for (const auto &result : results) {
      timing.merge(result.timing);
      matched_music_id = matched_music_id || result.matched_music_id;
      matched_difficulty = matched_difficulty || result.matched_difficulty;
      matched_version = matched_version || result.matched_version;

      for (const auto &warning : result.warnings) {
        warnings.push_back(warning);
        if (options.log_level == AssetsLogLevel::Verbose) {
          verbose_warning_buffer.push_back(warning);
        }
      }

      if (result.fatal_error.has_value()) {
        if (!first_fatal_error.has_value()) {
          first_fatal_error = result.fatal_error;
        }
        continue;
      }

      if (result.compiled_track.has_value()) {
        compiled_tracks[result.compiled_track->first] =
            result.compiled_track->second;
      }
      if (result.collection_entry.has_value()) {
        const std::string &collection_name = result.collection_entry->first;
        const std::string &music_id = result.collection_entry->second;
        const std::string dedupe_key = collection_name + "|" + music_id;
        if (collection_seen.insert(dedupe_key).second) {
          collections[collection_name].push_back(music_id);
        }
      }

      if (result.emit_track_output) {
        ++tracks_since_warning_flush;
        flush_verbose_warnings(false);
      }
    }
    flush_verbose_warnings(true);

    if (first_fatal_error.has_value()) {
      throw std::runtime_error(*first_fatal_error);
    }

    if (target_music_filters.active() && !matched_music_id) {
      throw std::runtime_error("Music id not found for filters: " +
                               join_tokens(target_music_filters.raw_tokens));
    }
    if (target_difficulty_filters.active() && !matched_difficulty) {
      throw std::runtime_error(
          "Difficulty not found for filters: id=" +
          join_tokens(target_music_filters.raw_tokens) +
          " difficulty=" + join_tokens(target_difficulty_filters.raw_tokens));
    }
    if (target_version_filters.active() && !matched_version) {
      throw std::runtime_error("Version not found for filters: " +
                               join_tokens(target_version_filters.raw_tokens));
    }

    emit_log_output(compiled_tracks, warnings, options.output_path,
                    options.log_tracks_json, options.log_level);
    if (options.compile_collections) {
      write_collections(options.output_path, collections);
    }
    if (timing_enabled) {
      emit_timing_summary(timing);
    }

    return kSuccess;
  } catch (const std::exception &ex) {
    std::cerr << "Program cannot proceed because of following error returned:\n"
              << ex.what() << "\n";
    return kFailure;
  }
}

} // namespace maiconv
