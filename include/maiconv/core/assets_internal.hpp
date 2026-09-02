#pragma once
// Internal to MaiConv_core asset compile. NOT the public assets.hpp.

#include "maiconv/core/assets.hpp"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace maiconv {

struct TrackInfo {
  struct DifficultyInfo {
    std::string constant_level;
    std::string display_level;
    std::string designer;
  };

  struct ChartOutputInfo {
    std::string chart_file_key;
    int output_difficulty = 1;
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
  std::string cue_id;
  std::string movie_id;
  bool movie_debug_placeholder = false;
  bool zero_based_difficulty = false;
  std::map<int, DifficultyInfo> difficulties;
  std::map<std::string, int> chart_output_difficulties;
  std::vector<ChartOutputInfo> chart_output_order;
  bool is_dx = false;
  bool is_utage = false;
};

struct NumericFilterSet {
  bool provided = false;
  std::set<std::string> exact;
  std::vector<std::regex> regex;
  std::vector<std::string> raw_tokens;

  bool active() const { return !exact.empty() || !regex.empty(); }

  bool matches(const std::string &value) const {
    if (!active()) {
      return true;
    }
    if (exact.find(value) != exact.end()) {
      return true;
    }
    return std::any_of(regex.begin(), regex.end(), [&](const std::regex &re) {
      return std::regex_match(value, re);
    });
  }
};

struct VersionFilterSet {
  bool provided = false;
  std::set<std::string> exact_version_ids;
  std::set<std::string> exact_version_names;
  std::vector<std::regex> regex;
  std::vector<std::string> raw_tokens;

  bool active() const {
    return !exact_version_ids.empty() || !exact_version_names.empty() ||
           !regex.empty();
  }
};

using AssetIndex = std::unordered_map<std::string, std::filesystem::path>;

inline std::string path_to_utf8(const std::filesystem::path &path) {
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

inline std::string path_to_generic_utf8(const std::filesystem::path &path) {
#if defined(_WIN32)
#if defined(__cpp_char8_t)
  const auto value = path.generic_u8string();
  std::string out;
  out.reserve(value.size());
  for (const auto ch : value) {
    out.push_back(static_cast<char>(ch));
  }
  return out;
#else
  return path.generic_u8string();
#endif
#else
  return path.generic_string();
#endif
}

// Hosted in assets_filter.cpp.
std::pair<std::string, std::string>
complete_version_fields(std::string version_id, std::string version);
std::string normalize_export_version_display(std::string version);
NumericFilterSet compile_music_id_filters(const AssetsOptions &options);
NumericFilterSet compile_difficulty_filters(const AssetsOptions &options);
VersionFilterSet compile_version_filters(const AssetsOptions &options);
bool matches_version_filter(const TrackInfo &info,
                            const VersionFilterSet &filters);

// Hosted in assets_parse.cpp.
TrackInfo default_track_info(const std::string &fallback_id);
std::optional<int> parse_ma2_difficulty(const std::filesystem::path &ma2_file);
TrackInfo parse_track_info_cached(const std::filesystem::path &music_xml,
                                  const std::string &fallback_id,
                                  bool *cache_hit);
int infer_output_difficulty(const TrackInfo &info,
                            const std::filesystem::path &ma2_file,
                            bool zero_based_difficulty);
std::string normalize_chart_path_key(const std::filesystem::path &path);
int source_root_numeric_priority(const std::filesystem::path &root);
bool is_more_preferred(int root_priority, const std::string &tie_break_key,
                       int other_root_priority,
                       const std::string &other_tie_break_key);
std::string root_pick_key(const std::filesystem::path &path);
std::vector<std::filesystem::path>
detect_asset_bases(const std::vector<std::filesystem::path> &source_roots,
                   const std::string &folder_name);
std::vector<AssetIndex>
build_asset_indexes_cached(const std::vector<std::filesystem::path> &bases,
                           const std::filesystem::path &cache_root,
                           std::size_t *cache_hits = nullptr,
                           std::size_t *cache_misses = nullptr);

} // namespace maiconv
