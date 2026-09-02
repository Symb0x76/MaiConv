#include "maiconv/core/assets_internal.hpp"

#include "maiconv/core/io.hpp"

#include <tinyxml2.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace maiconv {
namespace {

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

TrackInfo parse_track_info(const std::filesystem::path &music_xml,
                           const std::string &fallback_id) {
  TrackInfo info = default_track_info(fallback_id);

  tinyxml2::XMLDocument doc;
  std::string xml_payload;
  try {
    xml_payload = read_text_file(music_xml);
  } catch (...) {
    return info;
  }
  if (doc.Parse(xml_payload.c_str(), xml_payload.size()) !=
      tinyxml2::XML_SUCCESS) {
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
  if (info.version.empty() || info.version == "Unknown") {
    if (auto *version = find_first_element_by_name(root, "version")) {
      std::string parsed;
      if (auto *version_name = version->FirstChildElement("str")) {
        parsed = trim(element_text(version_name));
      } else {
        parsed = trim(element_text(version));
      }
      const bool is_numeric =
          !parsed.empty() &&
          std::all_of(parsed.begin(), parsed.end(),
                      [](unsigned char c) { return std::isdigit(c) != 0; });
      if (!parsed.empty() && !is_numeric) {
        info.version = parsed;
      }
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
  if (auto *cue = find_first_element_by_name(root, "cueName")) {
    if (auto *cue_id = cue->FirstChildElement("id")) {
      const std::string parsed = trim(element_text(cue_id));
      if (!parsed.empty() &&
          std::all_of(parsed.begin(), parsed.end(),
                      [](unsigned char c) { return std::isdigit(c) != 0; })) {
        info.cue_id = pad_music_id(parsed, 6);
      }
    }
  }
  if (auto *movie = find_first_element_by_name(root, "movieName")) {
    if (auto *movie_id = movie->FirstChildElement("id")) {
      const std::string parsed = trim(element_text(movie_id));
      if (!parsed.empty() &&
          std::all_of(parsed.begin(), parsed.end(),
                      [](unsigned char c) { return std::isdigit(c) != 0; })) {
        info.movie_id = pad_music_id(parsed, 6);
      }
    }
    if (auto *movie_name = movie->FirstChildElement("str")) {
      const std::string parsed = lower(trim(element_text(movie_name)));
      info.movie_debug_placeholder =
          parsed.size() >= 6 && parsed.rfind("debug_", 0) == 0;
    }
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
  info.cue_id = pad_music_id(info.cue_id, 6);
  info.movie_id = pad_music_id(info.movie_id, 6);
  info.is_dx = info.id.size() >= 2 && info.id[1] == '1';
  info.is_utage = to_int(info.genre_id, -1) == 107;

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
        const std::string chart_key =
            normalize_chart_path_key(std::filesystem::path(file_name));
        info.chart_output_difficulties[chart_key] = output_diff;
        info.chart_output_order.push_back({chart_key, output_diff});
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

int infer_inote_index(const std::filesystem::path &ma2_file,
                      bool zero_based_difficulty) {
  const auto parsed = parse_ma2_difficulty(ma2_file);
  if (!parsed.has_value()) {
    return 1;
  }
  return to_output_difficulty_index(*parsed, zero_based_difficulty);
}

std::int64_t file_time_to_ticks(std::filesystem::file_time_type time_point) {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             time_point.time_since_epoch())
      .count();
}

std::string asset_index_cache_key(const std::filesystem::path &base) {
  return lower(path_to_generic_utf8(base.lexically_normal()));
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

  std::string out;
  out.reserve(index.size() * 40 + 128);
  out += "MAICONV_ASSET_INDEX_V1\n";
  out += asset_index_cache_key(base);
  out.push_back('\n');
  out += std::to_string(file_time_to_ticks(base_mtime));
  out.push_back('\n');
  for (const auto &[_, absolute] : index) {
    (void)_;
    const auto relative =
        path_to_generic_utf8(absolute.lexically_relative(base));
    if (relative.empty() || relative == ".") {
      continue;
    }
    out += relative;
    out.push_back('\n');
  }

  try {
    write_text_file(cache_file, out);
  } catch (...) {
    // Best-effort cache write.
  }
}

} // namespace

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
  info.cue_id = info.id;
  info.movie_id = info.id;
  info.is_dx = info.id.size() >= 2 && info.id[1] == '1';
  info.is_utage = false;
  return info;
}

std::string normalize_chart_path_key(const std::filesystem::path &path) {
  return lower(path_to_generic_utf8(path.filename()));
}

int source_root_numeric_priority(const std::filesystem::path &root) {
  const std::string name = lower(path_to_utf8(root.filename()));
  if (name.empty()) {
    return -1;
  }

  std::size_t end = name.size();
  while (end > 0 &&
         std::isdigit(static_cast<unsigned char>(name[end - 1])) != 0) {
    --end;
  }
  if (end == name.size()) {
    return -1;
  }

  const std::string digits = name.substr(end);
  return to_int(digits, -1);
}

// Higher numeric root priority wins; ties break by larger (lowercased,
// normalized) path so ordering is deterministic across source roots.
bool is_more_preferred(int root_priority, const std::string &tie_break_key,
                       int other_root_priority,
                       const std::string &other_tie_break_key) {
  if (root_priority != other_root_priority) {
    return root_priority > other_root_priority;
  }
  return tie_break_key > other_tie_break_key;
}

std::string root_pick_key(const std::filesystem::path &path) {
  return lower(path_to_generic_utf8(path.lexically_normal()));
}

std::optional<int> parse_ma2_difficulty(const std::filesystem::path &ma2_file) {
  const std::string name = path_to_utf8(ma2_file.filename());
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

  const std::string key =
      lower(path_to_generic_utf8(music_xml.lexically_normal()));
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
  struct AssetBasePick {
    std::filesystem::path base;
    int root_priority = -1;
    std::string tie_break_key;
  };

  std::vector<AssetBasePick> picks;
  for (const auto &root : source_roots) {
    const auto candidate = root / folder_name;
    if (std::filesystem::exists(candidate) &&
        std::filesystem::is_directory(candidate)) {
      picks.push_back(AssetBasePick{
          candidate,
          source_root_numeric_priority(root),
          root_pick_key(candidate),
      });
    }
  }

  std::sort(picks.begin(), picks.end(),
            [](const AssetBasePick &lhs, const AssetBasePick &rhs) {
              return is_more_preferred(lhs.root_priority, lhs.tie_break_key,
                                       rhs.root_priority, rhs.tie_break_key);
            });

  std::vector<std::filesystem::path> bases;
  bases.reserve(picks.size());
  std::string last_key;
  for (const auto &pick : picks) {
    if (!last_key.empty() && pick.tie_break_key == last_key) {
      continue;
    }
    bases.push_back(pick.base);
    last_key = pick.tie_break_key;
  }
  return bases;
}

std::vector<AssetIndex>
build_asset_indexes_cached(const std::vector<std::filesystem::path> &bases,
                           const std::filesystem::path &cache_root,
                           std::size_t *cache_hits, std::size_t *cache_misses) {
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
          path_to_generic_utf8(entry.path().lexically_relative(base));
      index.emplace(lower(relative), entry.path());
    }
    write_asset_index_cache(cache_file, base, index);
    indexes.push_back(std::move(index));
  }

  return indexes;
}

} // namespace maiconv
