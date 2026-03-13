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
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>
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

TrackInfo default_track_info(const std::string &fallback_id) {
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
  info.is_dx = info.id.size() >= 2 && info.id[1] == '1';
  info.is_utage = info.id.size() >= 1 && info.id[0] == '1';
  return info;
}

bool is_reserved_music_id(const std::string &id) {
  const int numeric_id = to_int(id, -1);
  return numeric_id == 0 || numeric_id == 1;
}

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

std::optional<std::string_view>
version_name_from_version_id(std::string_view version_id) {
  static constexpr std::array<std::pair<std::string_view, std::string_view>, 24>
      kVersionIdMappings = {{
          {"1", "maimai"},    {"2", "maimai PLUS"},
          {"3", "GreeN"},     {"4", "GreeN PLUS"},
          {"5", "ORANGE"},    {"6", "ORANGE PLUS"},
          {"7", "PiNK"},      {"8", "PiNK PLUS"},
          {"9", "MURASAKi"},  {"10", "MURASAKi PLUS"},
          {"11", "MiLK"},     {"12", "MiLK PLUS"},
          {"13", "maimaDX"},  {"14", "maimaDX PLUS"},
          {"15", "Splash"},   {"16", "Splash PLUS"},
          {"17", "UNiVERSE"}, {"18", "UNiVERSE PLUS"},
          {"19", "FESTiVAL"}, {"20", "FESTiVAL PLUS"},
          {"21", "BUDDiES"},  {"22", "BUDDiES PLUS"},
          {"23", "PRiSM"},    {"24", "PRiSM PLUS"},
      }};
  for (const auto &[id, name] : kVersionIdMappings) {
    if (version_id == id) {
      return name;
    }
  }
  return std::nullopt;
}

bool is_decimal_number(std::string_view value) {
  return !value.empty() &&
         std::all_of(value.begin(), value.end(),
                     [](unsigned char c) { return std::isdigit(c) != 0; });
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
  TrackInfo info = default_track_info(fallback_id);

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

  if (const auto mapped_version = version_name_from_version_id(info.version_id);
      mapped_version.has_value()) {
    const bool unknown_or_empty =
        info.version.empty() || info.version == "Unknown";
    if (unknown_or_empty || is_decimal_number(info.version)) {
      info.version = std::string(*mapped_version);
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

struct TrackInfoCacheEntry {
  std::uintmax_t file_size = 0;
  std::filesystem::file_time_type last_write_time{};
  std::string fallback_id;
  TrackInfo info;
};

TrackInfo parse_track_info_cached(const std::filesystem::path &music_xml,
                                  const std::string &fallback_id,
                                  bool *cache_hit) {
  if (cache_hit != nullptr) {
    *cache_hit = false;
  }

  std::error_code size_ec;
  const std::uintmax_t file_size =
      std::filesystem::file_size(music_xml, size_ec);
  std::error_code time_ec;
  const auto last_write_time =
      std::filesystem::last_write_time(music_xml, time_ec);
  if (size_ec || time_ec) {
    return parse_track_info(music_xml, fallback_id);
  }

  const std::string key = lower(music_xml.lexically_normal().generic_string());
  static std::mutex cache_mutex;
  static std::unordered_map<std::string, TrackInfoCacheEntry> cache;

  {
    std::lock_guard<std::mutex> guard(cache_mutex);
    const auto it = cache.find(key);
    if (it != cache.end() && it->second.file_size == file_size &&
        it->second.last_write_time == last_write_time &&
        it->second.fallback_id == fallback_id) {
      if (cache_hit != nullptr) {
        *cache_hit = true;
      }
      return it->second.info;
    }
  }

  TrackInfo parsed = parse_track_info(music_xml, fallback_id);
  {
    std::lock_guard<std::mutex> guard(cache_mutex);
    cache[key] = TrackInfoCacheEntry{
        file_size,
        last_write_time,
        fallback_id,
        parsed,
    };
  }
  return parsed;
}

std::optional<int> parse_ma2_difficulty(const std::filesystem::path &ma2_file) {
  const std::string name = ma2_file.filename().string();
  if (name.size() < 7) {
    return std::nullopt;
  }

  const std::size_t offset = name.size() - 7;
  if (name[offset] != '_' || name[offset + 3] != '.' ||
      (name[offset + 4] != 'm' && name[offset + 4] != 'M') ||
      (name[offset + 5] != 'a' && name[offset + 5] != 'A') ||
      name[offset + 6] != '2') {
    return std::nullopt;
  }

  const unsigned char d0 = static_cast<unsigned char>(name[offset + 1]);
  const unsigned char d1 = static_cast<unsigned char>(name[offset + 2]);
  if (std::isdigit(d0) == 0 || std::isdigit(d1) == 0) {
    return std::nullopt;
  }
  return (name[offset + 1] - '0') * 10 + (name[offset + 2] - '0');
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

std::int64_t file_time_to_ticks(std::filesystem::file_time_type time_point) {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             time_point.time_since_epoch())
      .count();
}

std::string asset_index_cache_key(const std::filesystem::path &base) {
  return lower(base.lexically_normal().generic_string());
}

std::filesystem::path
asset_index_cache_path(const std::filesystem::path &cache_root,
                       const std::filesystem::path &base) {
  const std::string key = asset_index_cache_key(base);
  const std::size_t hash = std::hash<std::string>{}(key);
  return cache_root / ("asset_index_" + std::to_string(hash) + ".txt");
}

bool load_asset_index_cache(const std::filesystem::path &cache_file,
                            const std::filesystem::path &base,
                            AssetIndex &index) {
  if (!std::filesystem::exists(cache_file) ||
      !std::filesystem::is_regular_file(cache_file)) {
    return false;
  }

  std::vector<std::string> lines;
  try {
    lines = read_lines(cache_file);
  } catch (...) {
    return false;
  }
  if (lines.size() < 3) {
    return false;
  }
  if (lines[0] != "MAICONV_ASSET_INDEX_V1") {
    return false;
  }
  if (lines[1] != asset_index_cache_key(base)) {
    return false;
  }

  std::error_code time_ec;
  const auto base_mtime = std::filesystem::last_write_time(base, time_ec);
  if (time_ec) {
    return false;
  }
  const std::int64_t current_ticks = file_time_to_ticks(base_mtime);

  std::int64_t cached_ticks = 0;
  try {
    cached_ticks = std::stoll(lines[2]);
  } catch (...) {
    return false;
  }
  if (cached_ticks != current_ticks) {
    return false;
  }

  index.clear();
  index.reserve(lines.size() > 3 ? lines.size() - 3 : 0);
  for (std::size_t i = 3; i < lines.size(); ++i) {
    if (lines[i].empty()) {
      continue;
    }
    const std::filesystem::path relative = path_from_utf8(lines[i]);
    const std::filesystem::path absolute = base / relative;
    index.emplace(lower(lines[i]), absolute);
  }
  return true;
}

void write_asset_index_cache(const std::filesystem::path &cache_file,
                             const std::filesystem::path &base,
                             const AssetIndex &index) {
  std::error_code time_ec;
  const auto base_mtime = std::filesystem::last_write_time(base, time_ec);
  if (time_ec) {
    return;
  }

  std::ostringstream out;
  out << "MAICONV_ASSET_INDEX_V1\n";
  out << asset_index_cache_key(base) << "\n";
  out << file_time_to_ticks(base_mtime) << "\n";
  for (const auto &[_, absolute] : index) {
    (void)_;
    const auto relative = absolute.lexically_relative(base).generic_string();
    if (relative.empty() || relative == ".") {
      continue;
    }
    out << relative << "\n";
  }

  try {
    write_text_file(cache_file, out.str());
  } catch (...) {
    // Best-effort cache write.
  }
}

std::vector<AssetIndex>
build_asset_indexes_cached(const std::vector<std::filesystem::path> &bases,
                           const std::filesystem::path &cache_root,
                           std::size_t *cache_hits = nullptr,
                           std::size_t *cache_misses = nullptr) {
  if (cache_hits != nullptr) {
    *cache_hits = 0;
  }
  if (cache_misses != nullptr) {
    *cache_misses = 0;
  }

  std::filesystem::create_directories(cache_root);
  std::vector<AssetIndex> indexes;
  indexes.reserve(bases.size());

  for (const auto &base : bases) {
    AssetIndex index;
    if (!std::filesystem::exists(base) ||
        !std::filesystem::is_directory(base)) {
      indexes.push_back(std::move(index));
      continue;
    }

    const auto cache_file = asset_index_cache_path(cache_root, base);
    if (load_asset_index_cache(cache_file, base, index)) {
      if (cache_hits != nullptr) {
        ++(*cache_hits);
      }
      indexes.push_back(std::move(index));
      continue;
    }

    if (cache_misses != nullptr) {
      ++(*cache_misses);
    }
    for (const auto &entry :
         std::filesystem::recursive_directory_iterator(base)) {
      if (!entry.is_regular_file()) {
        continue;
      }
      const auto relative =
          entry.path().lexically_relative(base).generic_string();
      index.emplace(lower(relative), entry.path());
    }
    write_asset_index_cache(cache_file, base, index);
    indexes.push_back(std::move(index));
  }

  return indexes;
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

void push_warning(std::vector<std::string> &warnings, std::string warning) {
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
    const std::string key = lower(path.lexically_normal().generic_string());
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
  bool emit_track_output = false;
  bool incomplete = false;
  TrackInfo info;
  std::filesystem::path final_track_output;
  std::optional<std::pair<int, std::string>> compiled_track;
  std::optional<std::pair<std::string, std::string>> collection_entry;
  std::vector<std::string> warnings;
  std::optional<std::string> fatal_error;
  AssetsTimingSummary timing;
};

TrackProcessResult
process_track_folder(const std::filesystem::path &folder,
                     const AssetsOptions &options,
                     const std::optional<std::string> &target_music_id,
                     const std::optional<int> &target_difficulty,
                     const std::vector<std::filesystem::path> &music_bases,
                     const std::vector<std::filesystem::path> &cover_bases,
                     const std::vector<std::filesystem::path> &video_bases,
                     const std::vector<AssetIndex> &music_indexes,
                     const std::vector<AssetIndex> &cover_indexes,
                     const std::vector<AssetIndex> &video_indexes,
                     OutputPathMutexPool &output_path_mutex_pool) {
  TrackProcessResult result;
  try {
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

    if (is_reserved_music_id(info.id)) {
      return result;
    }

    if (target_music_id.has_value() && info.id != *target_music_id) {
      return result;
    }
    if (target_music_id.has_value()) {
      result.matched_music_id = true;
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
      return result;
    }

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
      return result;
    }
    if (target_difficulty.has_value()) {
      result.matched_difficulty = true;
    }

    const std::string category =
        category_name_for_layout(info, options.export_layout);
    std::filesystem::path category_folder = options.output_path;
    if (!category.empty()) {
      category_folder =
          append_utf8_path(category_folder, sanitize_folder_name(category));
    }

    const std::string display_name = export_display_title(info);
    const auto id_only_track_output = [&]() {
      return append_utf8_path(category_folder, info.id);
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

    const auto output_path_mutex = output_path_mutex_pool.acquire(track_output);
    std::lock_guard<std::mutex> output_guard(*output_path_mutex);

    std::filesystem::create_directories(category_folder);
    try {
      std::filesystem::create_directories(track_output);
    } catch (const std::filesystem::filesystem_error &) {
      if (options.music_id_folder_name) {
        throw;
      }
      track_output = id_only_track_output();
      std::filesystem::create_directories(track_output);
      used_id_folder_fallback = true;
    }
    if (used_id_folder_fallback) {
      push_warning(result.warnings,
                   "Folder name fallback to id for track " + info.id +
                       ": unsupported characters in export title");
    }
    std::filesystem::path final_track_output = track_output;

    Ma2Tokenizer tokenizer;
    Ma2Parser parser;
    Ma2Composer ma2_composer;
    SimaiComposer simai_composer;

    std::map<int, std::string> inotes;
    std::chrono::steady_clock::duration ma2_duration{};
    std::chrono::steady_clock::duration write_duration{};
    for (const auto &ma2_file : ma2_files) {
      const auto ma2_begin = std::chrono::steady_clock::now();
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

    if (!inotes.empty()) {
      const auto write_begin = std::chrono::steady_clock::now();
      const std::string payload =
          (options.format == ChartFormat::Maidata)
              ? compose_maidata_document(info, inotes, options.strict_decimal,
                                         options.maidata_level_mode)
              : compose_simai_document(info, inotes, options.strict_decimal);
      write_text_file(track_output / "maidata.txt", payload);
      write_duration += std::chrono::steady_clock::now() - write_begin;
    }
    if (write_duration != std::chrono::steady_clock::duration::zero()) {
      result.timing.write_zip.add(write_duration);
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

    const auto media_begin = std::chrono::steady_clock::now();
    std::vector<std::string> stems;
    std::vector<std::string> primary_candidates;
    std::vector<std::string> secondary_candidates;
    std::vector<std::string> lookup_names;

    if (!music_bases.empty()) {
      stems.clear();
      stems.reserve(3);
      append_unique_string(stems, "music" + info.id);
      append_unique_string(stems, "music" + non_dx_id);
      append_unique_string(stems, "music00" + short_id);

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
        const std::string ext = lower(compressed_audio.extension().string());
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
            incomplete = true;
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
            incomplete = true;
          }
        } else {
          push_warning(result.warnings,
                       "Music missing: " + info.name + " (" + info.id + ")");
          incomplete = true;
        }
      }
    }
    if (!cover_bases.empty()) {
      stems.clear();
      stems.reserve(4);
      append_unique_string(stems, "UI_Jacket_" + info.id);
      append_unique_string(stems, "UI_Jacket_00" + short_id);
      append_unique_string(stems, "ui_jacket_" + info.id);
      append_unique_string(stems, "ui_jacket_" + non_dx_id);

      append_suffix_candidates(stems, {".png", ".jpg", ".jpeg"},
                               primary_candidates);
      append_prefixed_suffix_candidates(
          stems, "jacket/", {".png", ".jpg", ".jpeg"}, secondary_candidates);
      std::vector<std::string> image_names;
      image_names.reserve(primary_candidates.size() +
                          secondary_candidates.size());
      image_names.insert(image_names.end(), primary_candidates.begin(),
                         primary_candidates.end());
      image_names.insert(image_names.end(), secondary_candidates.begin(),
                         secondary_candidates.end());

      append_suffix_candidates(stems, {".ab"}, primary_candidates);
      append_prefixed_suffix_candidates(stems, "jacket/", {".ab"},
                                        secondary_candidates);
      std::vector<std::string> ab_names;
      ab_names.reserve(primary_candidates.size() + secondary_candidates.size());
      ab_names.insert(ab_names.end(), primary_candidates.begin(),
                      primary_candidates.end());
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
        std::string ext = lower(cover_image.extension().string());
        if (ext.empty()) {
          ext = ".png";
        }
        std::filesystem::copy_file(
            cover_image, track_output / ("bg" + ext),
            std::filesystem::copy_options::overwrite_existing);
      } else {
        const auto cover_ab =
            first_found_candidate_in_order(found_cover, ab_names);
        if (!cover_ab.empty()) {
          const auto pseudo_image_ext =
              detect_image_extension_by_magic(cover_ab);
          if (pseudo_image_ext.has_value()) {
            std::filesystem::copy_file(
                cover_ab, track_output / ("bg" + *pseudo_image_ext),
                std::filesystem::copy_options::overwrite_existing);
          } else {
            if (!convert_ab_to_png(cover_ab, track_output / "bg.png")) {
              push_warning(
                  result.warnings,
                  "Cover conversion failed: " + path_to_utf8(cover_ab) +
                      " -> " + path_to_utf8(track_output / "bg.png"));
              incomplete = true;
            }
          }
        } else {
          push_warning(result.warnings,
                       "Cover missing: " + info.name + " (" + info.id + ")");
          incomplete = true;
        }
      }
    }
    if (!video_bases.empty()) {
      stems.clear();
      stems.reserve(4);
      append_unique_string(stems, info.id);
      append_unique_string(stems, non_dx_id);
      append_unique_string(stems, "00" + short_id);
      append_unique_string(stems, short_id);

      append_suffix_candidates(stems, {".mp4"}, primary_candidates);
      const std::vector<std::string> video_mp4_names = primary_candidates;
      append_suffix_candidates(stems, {".dat"}, primary_candidates);
      const std::vector<std::string> video_dat_names = primary_candidates;
      append_suffix_candidates(stems, {".usm"}, primary_candidates);
      const std::vector<std::string> video_usm_names = primary_candidates;

      lookup_names.clear();
      lookup_names.reserve(video_mp4_names.size() + video_dat_names.size() +
                           video_usm_names.size());
      lookup_names.insert(lookup_names.end(), video_mp4_names.begin(),
                          video_mp4_names.end());
      lookup_names.insert(lookup_names.end(), video_dat_names.begin(),
                          video_dat_names.end());
      lookup_names.insert(lookup_names.end(), video_usm_names.begin(),
                          video_usm_names.end());
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
          push_warning(result.warnings,
                       "Video missing: " + info.name + " (" + info.id + ")");
          incomplete = true;
        } else {
          if (!convert_dat_or_usm_to_mp4(video_source,
                                         track_output / "pv.mp4")) {
            push_warning(
                result.warnings,
                "Video conversion failed: " + path_to_utf8(video_source) +
                    " -> " + path_to_utf8(track_output / "pv.mp4"));
            incomplete = true;
          }
        }
      }
    }
    result.timing.media.add(std::chrono::steady_clock::now() - media_begin);

    if (incomplete && !options.ignore_incomplete_assets) {
      result.fatal_error = "Incomplete assets found. Use --ignore to continue.";
      return result;
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
                               options.streaming_assets_path.string());
    }
    if (options.jobs < 1) {
      throw std::runtime_error("jobs must be >= 1");
    }

    std::filesystem::create_directories(options.output_path);
    const bool timing_enabled =
        options.enable_timing || options.log_level == AssetsLogLevel::Verbose;

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

    const std::size_t worker_count =
        std::min(folders.size(), static_cast<std::size_t>(options.jobs));
    std::vector<TrackProcessResult> results(folders.size());
    OutputPathMutexPool output_path_mutex_pool;

    if (worker_count <= 1) {
      for (std::size_t i = 0; i < folders.size(); ++i) {
        results[i] = process_track_folder(
            folders[i], options, target_music_id, target_difficulty,
            music_bases, cover_bases, video_bases, music_indexes, cover_indexes,
            video_indexes, output_path_mutex_pool);
      }
    } else {
      std::atomic<std::size_t> next_index{0};
      std::vector<std::thread> workers;
      workers.reserve(worker_count);
      for (std::size_t i = 0; i < worker_count; ++i) {
        workers.emplace_back([&]() {
          while (true) {
            const std::size_t index =
                next_index.fetch_add(1, std::memory_order_relaxed);
            if (index >= folders.size()) {
              return;
            }
            results[index] = process_track_folder(
                folders[index], options, target_music_id, target_difficulty,
                music_bases, cover_bases, video_bases, music_indexes,
                cover_indexes, video_indexes, output_path_mutex_pool);
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
        emit_track_output(result.info, result.final_track_output,
                          result.incomplete, options.log_level);
      }
    }
    flush_verbose_warnings(true);

    if (first_fatal_error.has_value()) {
      throw std::runtime_error(*first_fatal_error);
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
