#include "maiconv/core/assets.hpp"

#include "maiconv/core/chart.hpp"
#include "maiconv/core/io.hpp"
#include "maiconv/core/ma2.hpp"
#include "maiconv/core/media.hpp"
#include "maiconv/core/simai.hpp"

#include <tinyxml2.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace maiconv {
namespace {

constexpr int kSuccess = 0;
constexpr int kFailure = 2;

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

int to_int(const std::string &s, int fallback = 0) {
  try {
    return std::stoi(s);
  } catch (...) {
    return fallback;
  }
}

struct TrackInfo {
  struct DifficultyInfo {
    std::string constant_level;
    std::string display_level;
    std::string designer;
  };

  std::string id;
  std::string short_id;
  std::string name;
  std::string sort_name;
  std::string genre_id;
  std::string genre;
  std::string version_id;
  std::string version;
  std::string composer;
  std::string bpm;
  bool zero_based_difficulty = false;
  std::map<int, DifficultyInfo> difficulties;
  std::map<std::string, int> chart_output_difficulties;
  bool is_dx = false;
  bool is_utage = false;
};

int to_output_difficulty_index(int ma2_diff, bool zero_based_difficulty) {
  if (zero_based_difficulty && ma2_diff >= 0 && ma2_diff <= 5) {
    return ma2_diff + 2;
  }
  if (ma2_diff >= 1 && ma2_diff <= 7) {
    return ma2_diff;
  }
  if (ma2_diff == 0) {
    return 1;
  }
  return 1;
}

int notes_slot_to_output_difficulty(std::size_t notes_slot, bool is_utage) {
  if (is_utage) {
    return 7;
  }
  if (notes_slot <= 5) {
    return static_cast<int>(notes_slot) + 2;
  }
  return 7;
}

std::string normalize_chart_path_key(const std::filesystem::path &path) {
  return lower(path.filename().generic_string());
}

std::string level_id_to_display_level(int level_id) {
  static const std::array<const char *, 25> kDisplayLevels = {
      "0",   "1",  "2",   "3",  "4",   "5",   "6",  "7",   "7+",
      "8",   "8+", "9",   "9+", "10",  "10+", "11", "11+", "12",
      "12+", "13", "13+", "14", "14+", "15",  "15+"};
  if (level_id < 0 || level_id >= static_cast<int>(kDisplayLevels.size())) {
    return "";
  }
  return kDisplayLevels[static_cast<std::size_t>(level_id)];
}

int constant_to_level_id(int level_x10) {
  if (level_x10 >= 156) {
    return 24;
  }
  if (level_x10 >= 150) {
    return 23;
  }
  if (level_x10 >= 146) {
    return 22;
  }
  if (level_x10 >= 140) {
    return 21;
  }
  if (level_x10 >= 136) {
    return 20;
  }
  if (level_x10 >= 130) {
    return 19;
  }
  if (level_x10 >= 126) {
    return 18;
  }
  if (level_x10 >= 120) {
    return 17;
  }
  if (level_x10 >= 116) {
    return 16;
  }
  if (level_x10 >= 110) {
    return 15;
  }
  if (level_x10 >= 106) {
    return 14;
  }
  if (level_x10 >= 100) {
    return 13;
  }
  if (level_x10 >= 96) {
    return 12;
  }
  if (level_x10 >= 90) {
    return 11;
  }
  if (level_x10 >= 86) {
    return 10;
  }
  if (level_x10 >= 80) {
    return 9;
  }
  if (level_x10 >= 76) {
    return 8;
  }
  if (level_x10 >= 0) {
    return level_x10 / 10;
  }
  return 0;
}

std::string constant_to_display_level(int level_int, int level_dec) {
  return level_id_to_display_level(
      constant_to_level_id(level_int * 10 + level_dec));
}

std::string element_text(tinyxml2::XMLElement *element) {
  if (element == nullptr) {
    return "";
  }
  if (auto *str = element->FirstChildElement("str")) {
    return str->GetText() == nullptr ? "" : str->GetText();
  }
  return element->GetText() == nullptr ? "" : element->GetText();
}

tinyxml2::XMLElement *find_first_element_by_name(tinyxml2::XMLNode *node,
                                                 const char *name) {
  if (node == nullptr) {
    return nullptr;
  }
  for (auto *elem = node->FirstChildElement(); elem != nullptr;
       elem = elem->NextSiblingElement()) {
    if (std::string(elem->Name()) == name) {
      return elem;
    }
    if (auto *nested = find_first_element_by_name(elem, name)) {
      return nested;
    }
  }
  return nullptr;
}

std::optional<int> parse_ma2_difficulty(const std::filesystem::path &ma2_file);

TrackInfo parse_track_info(const std::filesystem::path &music_xml,
                           const std::string &fallback_id) {
  TrackInfo info;
  info.id = fallback_id;
  info.short_id = std::to_string(to_int(fallback_id, 0));
  info.name = fallback_id;
  info.sort_name = fallback_id;
  info.genre_id = "0";
  info.genre = "Unknown";
  info.version_id = "0";
  info.version = "Unknown";
  info.composer = "Unknown";
  info.bpm = "120";

  tinyxml2::XMLDocument doc;
  if (doc.LoadFile(music_xml.string().c_str()) != tinyxml2::XML_SUCCESS) {
    return info;
  }

  auto *root = doc.RootElement();
  if (root == nullptr) {
    return info;
  }

  auto *name_element = find_first_element_by_name(root, "name");
  if (name_element != nullptr) {
    info.name = element_text(name_element);
  }
  if (auto *sort_name = find_first_element_by_name(root, "sortName")) {
    info.sort_name = element_text(sort_name);
  } else {
    info.sort_name = info.name;
  }
  if (auto *genre = find_first_element_by_name(root, "genreName")) {
    if (auto *genre_id = genre->FirstChildElement("id")) {
      const std::string parsed = trim(element_text(genre_id));
      if (!parsed.empty()) {
        info.genre_id = parsed;
      }
    }
    info.genre = element_text(genre);
  } else if (auto *genre = find_first_element_by_name(root, "genre")) {
    info.genre = element_text(genre);
  }
  if (auto *version = find_first_element_by_name(root, "AddVersion")) {
    if (auto *version_id = version->FirstChildElement("id")) {
      const std::string parsed = trim(element_text(version_id));
      if (!parsed.empty()) {
        info.version_id = parsed;
      }
    }
    if (auto *version_name = version->FirstChildElement("str")) {
      const std::string parsed = trim(element_text(version_name));
      if (!parsed.empty()) {
        info.version = parsed;
      }
    }
  }
  if (auto *version = find_first_element_by_name(root, "version")) {
    const std::string parsed = trim(element_text(version));
    if (info.version == "Unknown" && !parsed.empty()) {
      info.version = parsed;
    }
    if (info.version_id == "0" && !parsed.empty()) {
      info.version_id = parsed;
    }
  }
  if (auto *composer = find_first_element_by_name(root, "artistName")) {
    info.composer = element_text(composer);
  } else if (auto *composer = find_first_element_by_name(root, "artist")) {
    info.composer = element_text(composer);
  }
  if (auto *bpm = find_first_element_by_name(root, "bpm")) {
    info.bpm = element_text(bpm);
  }

  std::string candidate_id;
  if (name_element != nullptr) {
    if (auto *nested_id = name_element->FirstChildElement("id")) {
      candidate_id = trim(element_text(nested_id));
    }
  }
  if (!candidate_id.empty() &&
      std::all_of(candidate_id.begin(), candidate_id.end(),
                  [](unsigned char c) { return std::isdigit(c) != 0; })) {
    info.short_id = std::to_string(to_int(candidate_id, 0));
    info.id = pad_music_id(candidate_id, 6);
  }

  info.id = pad_music_id(info.id, 6);
  info.is_dx = info.id.size() >= 2 && info.id[1] == '1';
  info.is_utage = info.id.size() >= 1 && info.id[0] == '1';

  if (auto *notes_data = find_first_element_by_name(root, "notesData")) {
    struct RawDifficultyInfo {
      int ma2_diff;
      TrackInfo::DifficultyInfo info;
    };
    std::vector<RawDifficultyInfo> raw_difficulties;
    bool has_explicit_zero_based_diff = false;

    std::size_t notes_slot = 0;
    for (auto *notes = notes_data->FirstChildElement("Notes"); notes != nullptr;
         notes = notes->NextSiblingElement("Notes"), ++notes_slot) {
      bool enabled = true;
      if (auto *enable_node = notes->FirstChildElement("isEnable")) {
        const std::string raw = lower(trim(element_text(enable_node)));
        enabled = raw.empty() || raw == "true" || raw == "1";
      }
      if (!enabled) {
        continue;
      }

      std::string file_name;
      if (auto *file = notes->FirstChildElement("file")) {
        if (auto *path = file->FirstChildElement("path")) {
          file_name = trim(element_text(path));
        }
      }

      std::optional<int> parsed_diff;
      if (!file_name.empty()) {
        parsed_diff = parse_ma2_difficulty(std::filesystem::path(file_name));
      }

      const int ma2_diff = parsed_diff.value_or(-1);
      if (ma2_diff == 0) {
        has_explicit_zero_based_diff = true;
      }

      int level_int =
          to_int(element_text(notes->FirstChildElement("level")), 0);
      int level_dec =
          to_int(element_text(notes->FirstChildElement("levelDecimal")), 0);
      if (level_int == 0 && level_dec == 0) {
        continue;
      }

      std::string constant_level =
          std::to_string(level_int) + "." + std::to_string(level_dec);
      std::string display_level;
      const int level_id =
          to_int(element_text(notes->FirstChildElement("musicLevelID")), -1);
      if (level_id >= 0) {
        display_level = level_id_to_display_level(level_id);
      }
      if (display_level.empty()) {
        display_level = constant_to_display_level(level_int, level_dec);
      }
      std::string designer;
      if (auto *notes_designer = notes->FirstChildElement("notesDesigner")) {
        if (auto *designer_name = notes_designer->FirstChildElement("str")) {
          designer = trim(element_text(designer_name));
        }
      }

      if (ma2_diff >= 0) {
        const int output_diff =
            notes_slot_to_output_difficulty(notes_slot, info.is_utage);
        raw_difficulties.push_back(
            {ma2_diff, {constant_level, display_level, designer}});
        info.chart_output_difficulties[normalize_chart_path_key(
            std::filesystem::path(file_name))] = output_diff;
        info.difficulties[output_diff] = {constant_level, display_level,
                                          designer};
      }
    }

    info.zero_based_difficulty = has_explicit_zero_based_diff;
    if (info.chart_output_difficulties.empty()) {
      for (const auto &entry : raw_difficulties) {
        const int output_diff = to_output_difficulty_index(
            entry.ma2_diff, info.zero_based_difficulty);
        info.difficulties[output_diff] = entry.info;
      }
    }
  }

  return info;
}

std::optional<int> parse_ma2_difficulty(const std::filesystem::path &ma2_file) {
  static const std::regex suffix_re(R"(_(\d{2})\.ma2$)", std::regex::icase);
  std::smatch match;
  const std::string name = ma2_file.filename().string();
  if (!std::regex_search(name, match, suffix_re)) {
    return std::nullopt;
  }
  return std::stoi(match[1].str());
}

int infer_inote_index(const std::filesystem::path &ma2_file,
                      bool zero_based_difficulty) {
  const auto parsed = parse_ma2_difficulty(ma2_file);
  if (!parsed.has_value()) {
    return 1;
  }
  return to_output_difficulty_index(*parsed, zero_based_difficulty);
}

int infer_output_difficulty(const TrackInfo &info,
                            const std::filesystem::path &ma2_file,
                            bool zero_based_difficulty) {
  const auto it =
      info.chart_output_difficulties.find(normalize_chart_path_key(ma2_file));
  if (it != info.chart_output_difficulties.end()) {
    return it->second;
  }
  return infer_inote_index(ma2_file, zero_based_difficulty);
}

std::vector<std::filesystem::path>
detect_asset_bases(const std::vector<std::filesystem::path> &source_roots,
                   const std::string &folder_name) {
  std::vector<std::filesystem::path> bases;
  for (const auto &root : source_roots) {
    const auto candidate = root / folder_name;
    if (std::filesystem::exists(candidate) &&
        std::filesystem::is_directory(candidate)) {
      bases.push_back(candidate);
    }
  }
  std::sort(bases.begin(), bases.end());
  bases.erase(std::unique(bases.begin(), bases.end()), bases.end());
  return bases;
}

using AssetIndex = std::unordered_map<std::string, std::filesystem::path>;

std::vector<AssetIndex>
build_asset_indexes(const std::vector<std::filesystem::path> &bases) {
  std::vector<AssetIndex> indexes;
  indexes.reserve(bases.size());

  for (const auto &base : bases) {
    AssetIndex index;
    if (std::filesystem::exists(base) && std::filesystem::is_directory(base)) {
      for (const auto &entry :
           std::filesystem::recursive_directory_iterator(base)) {
        if (!entry.is_regular_file()) {
          continue;
        }
        const auto relative =
            entry.path().lexically_relative(base).generic_string();
        index.emplace(lower(relative), entry.path());
      }
    }
    indexes.push_back(std::move(index));
  }

  return indexes;
}

std::filesystem::path
find_asset_candidate_in_indexes(const std::vector<AssetIndex> &indexes,
                                const std::vector<std::string> &names) {
  for (const auto &index : indexes) {
    for (const auto &name : names) {
      const auto it = index.find(lower(name));
      if (it != index.end()) {
        return it->second;
      }
    }
  }
  return {};
}

std::string path_to_utf8(const std::filesystem::path &path) {
#if defined(_WIN32)
#if defined(__cpp_char8_t)
  const auto value = path.u8string();
  std::string out;
  out.reserve(value.size());
  for (const auto ch : value) {
    out.push_back(static_cast<char>(ch));
  }
  return out;
#else
  return path.u8string();
#endif
#else
  return path.string();
#endif
}

std::string category_name_for_layout(const TrackInfo &info,
                                     AssetsExportLayout layout) {
  const auto normalize_layout_genre = [](std::string_view genre) {
    static const std::array<std::pair<std::string_view, std::string_view>, 4>
        kGenreMappings = {{
            {"POPSアニメ", "POPS＆アニメ"},
            {"niconicoボーカロイド", "niconico＆ボーカロイド"},
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
  case AssetsExportLayout::Version:
    return info.version.empty() ? "Unknown"
                                : normalize_layout_version(info.version);
  }
  return "";
}

std::string json_escape(const std::string &value) {
  std::ostringstream out;
  for (char c : value) {
    switch (c) {
    case '\\':
      out << "\\\\";
      break;
    case '"':
      out << "\\\"";
      break;
    case '\n':
      out << "\\n";
      break;
    case '\r':
      out << "\\r";
      break;
    case '\t':
      out << "\\t";
      break;
    default:
      out << c;
      break;
    }
  }
  return out.str();
}

std::string normalize_maidata_genre(std::string_view genre) {
  static const std::array<std::pair<std::string_view, std::string_view>, 4>
      kGenreMappings = {{
          {"POPSアニメ", "POPS＆アニメ"},
          {"niconicoボーカロイド", "niconico＆ボーカロイド"},
          {"ゲームバラエティ", "ゲーム&バラエティ"},
          {"オンゲキCHUNITHM", "オンゲキ&CHUNITHM"},
      }};
  for (const auto &[from, to] : kGenreMappings) {
    if (genre == from) {
      return std::string(to);
    }
  }
  return std::string(genre);
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

std::string export_display_title(const TrackInfo &info) {
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
  return title;
}

void push_warning(std::vector<std::string> &warnings, AssetsLogLevel log_level,
                  std::string warning) {
  if (log_level == AssetsLogLevel::Verbose) {
    std::cerr << "Warning: " << warning << "\n";
  }
  warnings.push_back(std::move(warning));
}

void emit_track_output(const TrackInfo &info,
                       const std::filesystem::path &output_path,
                       bool incomplete, AssetsLogLevel log_level) {
  if (log_level == AssetsLogLevel::Quiet) {
    return;
  }

  std::cout << (incomplete ? "Incomplete: " : "Completed: ") << info.id << " "
            << export_display_title(info);
  if (log_level == AssetsLogLevel::Verbose) {
    std::cout << " -> " << path_to_utf8(output_path);
  }
  std::cout << "\n";
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
    std::ostringstream json;
    json << "{\n";
    std::size_t i = 0;
    for (const auto &[id, name] : compiled) {
      json << "  \"" << id << "\": \"" << json_escape(name) << "\"";
      if (++i < compiled.size()) {
        json << ",";
      }
      json << "\n";
    }
    json << "}\n";
    write_text_file(output / "_index.json", json.str());
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
    std::ostringstream manifest;
    manifest << "{\n";
    manifest << "  \"name\": \"" << json_escape(name) << "\",\n";
    manifest << "  \"levelIds\": [";
    for (std::size_t i = 0; i < ids.size(); ++i) {
      if (i != 0) {
        manifest << ", ";
      }
      manifest << "\"" << ids[i] << "\"";
    }
    manifest << "]\n";
    manifest << "}\n";
    write_text_file(folder / "manifest.json", manifest.str());
  }
}

bool zip_and_remove(const std::filesystem::path &folder) {
  (void)folder;
  return false;
}

std::string compose_simai_document(const TrackInfo &info,
                                   const std::map<int, std::string> &inotes,
                                   bool strict_decimal) {
  (void)info;
  (void)strict_decimal;
  std::ostringstream out;
  for (const auto &[diff, body] : inotes) {
    out << "&inote_" << diff << "=" << body << "\n\n";
  }
  return out.str();
}

std::string compose_maidata_document(const TrackInfo &info,
                                     const std::map<int, std::string> &inotes,
                                     bool strict_decimal,
                                     MaidataLevelMode level_mode) {
  (void)strict_decimal;
  std::ostringstream out;
  out << "&title=" << export_display_title(info) << "\n";
  out << "&artist=" << normalize_maidata_metadata_value(info.composer) << "\n";
  out << "&wholebpm=" << info.bpm << "\n";
  out << "&first=0.0333\n";
  out << "&shortid=" << info.short_id << "\n";
  out << "&genreid=" << info.genre_id << "\n";
  out << "&genre="
      << normalize_maidata_metadata_value(normalize_maidata_genre(info.genre))
      << "\n";
  out << "&versionid=" << info.version_id << "\n";
  out << "&version=" << normalize_maidata_metadata_value(info.version) << "\n";
  out << "&chartconverter=maiconv\n";

  for (const auto &[diff, body] : inotes) {
    const auto difficulty_meta = info.difficulties.find(diff);
    out << "&lv_" << diff << "=";
    if (difficulty_meta != info.difficulties.end()) {
      out << (level_mode == MaidataLevelMode::Display
                  ? difficulty_meta->second.display_level
                  : difficulty_meta->second.constant_level);
    }
    out << "\n";
    out << "&des_" << diff << "=";
    if (difficulty_meta != info.difficulties.end()) {
      out << normalize_maidata_metadata_value(difficulty_meta->second.designer);
    }
    out << "\n";
    out << "&inote_" << diff << "=" << body << "\n\n";
  }
  return out.str();
}

} // namespace

int run_compile_assets(const AssetsOptions &options) {
  try {
    if (options.streaming_assets_path.empty() || options.output_path.empty()) {
      throw std::runtime_error("input path and output path are required");
    }
    if (!std::filesystem::exists(options.streaming_assets_path)) {
      throw std::runtime_error("Input folder not found: " +
                               options.streaming_assets_path.string());
    }

    std::filesystem::create_directories(options.output_path);

    std::optional<std::string> target_music_id;
    if (options.target_music_id.has_value() &&
        !options.target_music_id->empty()) {
      const std::string padded = pad_music_id(*options.target_music_id, 6);
      if (!std::all_of(padded.begin(), padded.end(),
                       [](unsigned char c) { return std::isdigit(c) != 0; })) {
        throw std::runtime_error("music id must be numeric");
      }
      target_music_id = padded;
    }

    const auto is_reserved_music_id = [](const std::string &id) {
      const int numeric_id = to_int(id, -1);
      return numeric_id == 0 || numeric_id == 1;
    };

    if (target_music_id.has_value() && is_reserved_music_id(*target_music_id)) {
      std::cout << "Skipping reserved music id: " << *target_music_id << "\n";
      return kSuccess;
    }

    std::optional<int> target_difficulty = options.target_difficulty;
    if (target_difficulty.has_value()) {
      if (!target_music_id.has_value()) {
        throw std::runtime_error("difficulty requires music id");
      }
      if (*target_difficulty < 1 || *target_difficulty > 7) {
        throw std::runtime_error("difficulty must be in range 1..7");
      }
    }

    bool matched_music_id = false;
    bool matched_difficulty = false;

    Ma2Tokenizer tokenizer;
    Ma2Parser parser;
    Ma2Composer ma2_composer;
    SimaiComposer simai_composer;

    std::map<int, std::string> compiled_tracks;
    std::vector<std::string> warnings;
    std::map<std::string, std::vector<std::string>> collections;
    std::set<std::string> collection_seen;

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

    if (options.music_path.has_value()) {
      music_bases.push_back(*options.music_path);
    } else {
      music_bases = detect_asset_bases(source_roots, "SoundData");
    }

    if (options.cover_path.has_value()) {
      cover_bases.push_back(*options.cover_path);
    } else {
      cover_bases = detect_asset_bases(source_roots, "AssetBundleImages");
    }

    if (options.video_path.has_value()) {
      video_bases.push_back(*options.video_path);
    } else {
      video_bases = detect_asset_bases(source_roots, "MovieData");
    }

    const auto music_indexes = build_asset_indexes(music_bases);
    const auto cover_indexes = build_asset_indexes(cover_bases);
    const auto video_indexes = build_asset_indexes(video_bases);

    std::vector<std::filesystem::path> folders;
    for (const auto &root : source_roots) {
      const auto music_root = root / "music";
      if (!std::filesystem::exists(music_root) ||
          !std::filesystem::is_directory(music_root)) {
        continue;
      }
      for (const auto &entry :
           std::filesystem::directory_iterator(music_root)) {
        if (entry.is_directory()) {
          folders.push_back(entry.path());
        }
      }
    }
    std::sort(folders.begin(), folders.end());
    if (folders.empty()) {
      throw std::runtime_error("No music folders found under input path: " +
                               options.streaming_assets_path.string());
    }

    for (const auto &folder : folders) {
      const std::string folder_name = folder.filename().string();
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
        info = parse_track_info(xml_path, fallback_id);
      } else {
        info.id = fallback_id;
        info.short_id = std::to_string(to_int(fallback_id, 0));
        info.name = fallback_id;
        info.sort_name = fallback_id;
        info.genre_id = "0";
        info.genre = "Unknown";
        info.version_id = "0";
        info.version = "Unknown";
        info.composer = "Unknown";
        info.bpm = "120";
        info.is_dx = info.id.size() >= 2 && info.id[1] == '1';
        info.is_utage = info.id.size() >= 1 && info.id[0] == '1';
      }

      if (is_reserved_music_id(info.id)) {
        continue;
      }

      if (target_music_id.has_value() && info.id != *target_music_id) {
        continue;
      }
      if (target_music_id.has_value()) {
        matched_music_id = true;
      }

      std::vector<std::filesystem::path> all_ma2_files;
      for (const auto &entry : std::filesystem::directory_iterator(folder)) {
        if (!entry.is_regular_file() ||
            lower(entry.path().extension().string()) != ".ma2") {
          continue;
        }
        all_ma2_files.push_back(entry.path());
      }
      std::sort(all_ma2_files.begin(), all_ma2_files.end());
      if (all_ma2_files.empty()) {
        continue;
      }

      const std::string category =
          category_name_for_layout(info, options.export_layout);
      std::filesystem::path category_folder = options.output_path;
      if (!category.empty()) {
        category_folder =
            append_utf8_path(category_folder, sanitize_folder_name(category));
      }
      std::filesystem::create_directories(category_folder);

      const std::string display_name = export_display_title(info);
      std::string folder_stem =
          options.music_id_folder_name
              ? info.id
              : (info.id + "_" + sanitize_folder_name(display_name));
      std::filesystem::path track_output =
          append_utf8_path(category_folder, folder_stem);
      std::filesystem::create_directories(track_output);
      std::filesystem::path final_track_output = track_output;

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

      std::vector<std::filesystem::path> ma2_files;
      for (const auto &ma2_file : all_ma2_files) {
        if (target_difficulty.has_value() &&
            infer_output_difficulty(info, ma2_file, zero_based_difficulty) !=
                *target_difficulty) {
          continue;
        }
        ma2_files.push_back(ma2_file);
      }
      if (ma2_files.empty()) {
        continue;
      }
      if (target_difficulty.has_value()) {
        matched_difficulty = true;
      }

      std::map<int, std::string> inotes;
      for (const auto &ma2_file : ma2_files) {
        const auto chart = parser.parse(tokenizer.tokenize_file(ma2_file));
        Chart transformed = chart;
        if (options.rotate.has_value()) {
          transformed.rotate(*options.rotate);
        }
        if (options.shift_ticks != 0) {
          transformed.shift_by_offset(options.shift_ticks);
        }

        if (options.format == ChartFormat::Simai ||
            options.format == ChartFormat::SimaiFes ||
            options.format == ChartFormat::Maidata) {
          const int inote =
              infer_output_difficulty(info, ma2_file, zero_based_difficulty);
          inotes[inote] = simai_composer.compose_chart(transformed);
        } else {
          const std::string text =
              ma2_composer.compose(transformed, options.format);
          write_text_file(track_output / "result.ma2", text);
        }
      }

      if (!inotes.empty()) {
        const std::string payload =
            (options.format == ChartFormat::Maidata)
                ? compose_maidata_document(info, inotes, options.strict_decimal,
                                           options.maidata_level_mode)
                : compose_simai_document(info, inotes, options.strict_decimal);
        write_text_file(track_output / "maidata.txt", payload);
      }

      bool incomplete = false;

      int id_number = to_int(info.id, 0);
      if (id_number < 0) {
        id_number = -id_number;
      }
      const std::string non_dx_id =
          pad_music_id(std::to_string(id_number % 10000), 6);
      const std::string short_id =
          non_dx_id.size() >= 2 ? non_dx_id.substr(2) : non_dx_id;

      auto append_unique = [](std::vector<std::string> &values,
                              const std::string &value) {
        if (std::find(values.begin(), values.end(), value) == values.end()) {
          values.push_back(value);
        }
      };

      if (!music_bases.empty()) {
        std::vector<std::string> audio_bases;
        append_unique(audio_bases, "music" + info.id);
        append_unique(audio_bases, "music" + non_dx_id);
        append_unique(audio_bases, "music00" + short_id);

        std::vector<std::string> compressed_audio_names;
        for (const auto &base_name : audio_bases) {
          compressed_audio_names.push_back(base_name + ".mp3");
          compressed_audio_names.push_back(base_name + ".ogg");
        }

        const auto compressed_audio = find_asset_candidate_in_indexes(
            music_indexes, compressed_audio_names);
        if (!compressed_audio.empty()) {
          const std::string ext = lower(compressed_audio.extension().string());
          if (ext == ".mp3") {
            std::filesystem::copy_file(
                compressed_audio, track_output / "track.mp3",
                std::filesystem::copy_options::overwrite_existing);
          } else {
            if (!convert_audio_to_mp3(compressed_audio,
                                      track_output / "track.mp3")) {
              push_warning(
                  warnings, options.log_level,
                  "Audio conversion failed: " + path_to_utf8(compressed_audio) +
                      " -> " + path_to_utf8(track_output / "track.mp3"));
              incomplete = true;
            }
          }
        } else {
          std::vector<std::string> acb_names;
          std::vector<std::string> awb_names;
          for (const auto &base_name : audio_bases) {
            acb_names.push_back(base_name + ".acb");
            awb_names.push_back(base_name + ".awb");
          }

          const auto acb_source =
              find_asset_candidate_in_indexes(music_indexes, acb_names);
          const auto awb_source =
              find_asset_candidate_in_indexes(music_indexes, awb_names);
          if (!acb_source.empty() && !awb_source.empty()) {
            if (!convert_acb_awb_to_mp3(acb_source, awb_source,
                                        track_output / "track.mp3")) {
              push_warning(
                  warnings, options.log_level,
                  "Audio conversion failed: " + path_to_utf8(acb_source) +
                      " + " + path_to_utf8(awb_source) + " -> " +
                      path_to_utf8(track_output / "track.mp3"));
              incomplete = true;
            }
          } else {
            push_warning(warnings, options.log_level,
                         "Music missing: " + info.name + " (" + info.id + ")");
            incomplete = true;
          }
        }
      }
      if (!cover_bases.empty()) {
        std::vector<std::string> jacket_stems;
        append_unique(jacket_stems, "UI_Jacket_" + info.id);
        append_unique(jacket_stems, "UI_Jacket_00" + short_id);
        append_unique(jacket_stems, "ui_jacket_" + info.id);
        append_unique(jacket_stems, "ui_jacket_" + non_dx_id);

        std::vector<std::string> image_names;
        std::vector<std::string> ab_names;
        for (const auto &stem : jacket_stems) {
          image_names.push_back(stem + ".png");
          image_names.push_back(stem + ".jpg");
          image_names.push_back(stem + ".jpeg");
          image_names.push_back("jacket/" + stem + ".png");
          image_names.push_back("jacket/" + stem + ".jpg");
          image_names.push_back("jacket/" + stem + ".jpeg");
          ab_names.push_back(stem + ".ab");
          ab_names.push_back("jacket/" + stem + ".ab");
        }

        const auto cover_image =
            find_asset_candidate_in_indexes(cover_indexes, image_names);
        if (!cover_image.empty()) {
          std::string ext = lower(cover_image.extension().string());
          if (ext.empty()) {
            ext = ".png";
          }
          std::filesystem::copy_file(
              cover_image, track_output / ("bg" + ext),
              std::filesystem::copy_options::overwrite_existing);
        } else {
          const auto cover_ab =
              find_asset_candidate_in_indexes(cover_indexes, ab_names);
          if (!cover_ab.empty()) {
            const auto pseudo_image_ext =
                detect_image_extension_by_magic(cover_ab);
            if (pseudo_image_ext.has_value()) {
              // Some packs ship ui_jacket_*.ab as raw PNG/JPEG bytes instead of
              // Unity bundles.
              std::filesystem::copy_file(
                  cover_ab, track_output / ("bg" + *pseudo_image_ext),
                  std::filesystem::copy_options::overwrite_existing);
            } else {
              if (!convert_ab_to_png(cover_ab, track_output / "bg.png")) {
                push_warning(
                    warnings, options.log_level,
                    "Cover conversion failed: " + path_to_utf8(cover_ab) +
                        " -> " + path_to_utf8(track_output / "bg.png"));
                incomplete = true;
              }
            }
          } else {
            push_warning(warnings, options.log_level,
                         "Cover missing: " + info.name + " (" + info.id + ")");
            incomplete = true;
          }
        }
      }
      if (!video_bases.empty()) {
        std::vector<std::string> movie_stems;
        append_unique(movie_stems, info.id);
        append_unique(movie_stems, non_dx_id);
        append_unique(movie_stems, "00" + short_id);
        append_unique(movie_stems, short_id);

        std::vector<std::string> video_mp4_names;
        std::vector<std::string> video_dat_names;
        std::vector<std::string> video_usm_names;
        for (const auto &stem : movie_stems) {
          video_mp4_names.push_back(stem + ".mp4");
          video_dat_names.push_back(stem + ".dat");
          video_usm_names.push_back(stem + ".usm");
        }

        auto video_source =
            find_asset_candidate_in_indexes(video_indexes, video_mp4_names);
        if (!video_source.empty()) {
          std::filesystem::copy_file(
              video_source, track_output / "pv.mp4",
              std::filesystem::copy_options::overwrite_existing);
        } else {
          video_source =
              find_asset_candidate_in_indexes(video_indexes, video_dat_names);
          if (video_source.empty()) {
            video_source =
                find_asset_candidate_in_indexes(video_indexes, video_usm_names);
          }

          if (video_source.empty()) {
            push_warning(warnings, options.log_level,
                         "Video missing: " + info.name + " (" + info.id + ")");
            incomplete = true;
          } else {
            if (!convert_dat_or_usm_to_mp4(video_source,
                                           track_output / "pv.mp4")) {
              push_warning(
                  warnings, options.log_level,
                  "Video conversion failed: " + path_to_utf8(video_source) +
                      " -> " + path_to_utf8(track_output / "pv.mp4"));
              incomplete = true;
            }
          }
        }
      }
      if (incomplete && !options.ignore_incomplete_assets) {
        throw std::runtime_error(
            "Incomplete assets found. Use --ignore to continue.");
      }

      if (incomplete) {
        auto incomplete_path = track_output;
        incomplete_path += "_Incomplete";
        if (std::filesystem::exists(incomplete_path)) {
          std::filesystem::remove_all(incomplete_path);
        }
        std::filesystem::rename(track_output, incomplete_path);
        final_track_output = incomplete_path;
      } else {
        compiled_tracks[to_int(info.id)] = info.name;
        const std::string collection_name =
            category.empty() ? "default" : category;
        const std::string dedupe_key = collection_name + "|" + info.id;
        if (collection_seen.insert(dedupe_key).second) {
          collections[collection_name].push_back(info.id);
        }

        if (options.export_zip) {
          if (!zip_and_remove(track_output)) {
            push_warning(warnings, options.log_level,
                         "Zip export failed: " + path_to_utf8(track_output));
          }
        }
      }

      emit_track_output(info, final_track_output, incomplete,
                        options.log_level);
    }

    if (target_music_id.has_value() && !matched_music_id) {
      throw std::runtime_error("Music id not found: " + *target_music_id);
    }
    if (target_difficulty.has_value() && !matched_difficulty) {
      throw std::runtime_error(
          "Difficulty not found for music id: " + *target_music_id +
          " difficulty=" + std::to_string(*target_difficulty));
    }

    emit_log_output(compiled_tracks, warnings, options.output_path,
                    options.log_tracks_json, options.log_level);
    if (options.compile_collections) {
      write_collections(options.output_path, collections);
    }

    return kSuccess;
  } catch (const std::exception &ex) {
    std::cerr << "Program cannot proceed because of following error returned:\n"
              << ex.what() << "\n";
    return kFailure;
  }
}

} // namespace maiconv
