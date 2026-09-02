#include "maiconv/core/assets_internal.hpp"

#include "maiconv/core/io.hpp"

#include <algorithm>
#include <cctype>
#include <regex>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace maiconv {
namespace {

std::string normalize_version_name_key(std::string value) {
  value = lower(trim(value));
  value.erase(
      std::remove_if(value.begin(), value.end(),
                     [](unsigned char c) { return std::isspace(c) != 0; }),
      value.end());
  return value;
}

std::string version_name_from_id(std::string_view version_id) {
  static const std::unordered_map<std::string, std::string> kVersionNameById = {
      {"0", "maimai"},         {"1", "maimai PLUS"},   {"2", "GreeN"},
      {"3", "GreeN PLUS"},     {"4", "ORANGE"},        {"5", "ORANGE PLUS"},
      {"6", "PiNK"},           {"7", "PiNK PLUS"},     {"8", "MURASAKi"},
      {"9", "MURASAKi PLUS"},  {"10", "MiLK"},         {"11", "MiLK PLUS"},
      {"12", "FiNALE"},        {"13", "maimaDX"},      {"14", "maimaDX PLUS"},
      {"15", "Splash"},        {"16", "Splash PLUS"},  {"17", "UNiVERSE"},
      {"18", "UNiVERSE PLUS"}, {"19", "FESTiVAL"},     {"20", "FESTiVAL PLUS"},
      {"21", "BUDDiES"},       {"22", "BUDDiES PLUS"}, {"23", "PRiSM"},
      {"24", "PRiSM PLUS"},    {"25", "CiRCLE"},
  };

  const std::string key = trim(std::string(version_id));
  const auto it = kVersionNameById.find(key);
  if (it == kVersionNameById.end()) {
    return {};
  }
  return it->second;
}

std::string version_id_from_name(std::string_view version_name) {
  static const std::unordered_map<std::string, std::string> kVersionIdByName = {
      {"maimai", "0"},       {"maimaiplus", "1"},    {"green", "2"},
      {"greenplus", "3"},    {"orange", "4"},        {"orangeplus", "5"},
      {"pink", "6"},         {"pinkplus", "7"},      {"murasaki", "8"},
      {"murasakiplus", "9"}, {"milk", "10"},         {"milkplus", "11"},
      {"finale", "12"},      {"maimadx", "13"},      {"maimadxplus", "14"},
      {"deluxe", "13"},      {"deluxeplus", "14"},   {"splash", "15"},
      {"splashplus", "16"},  {"universe", "17"},     {"universeplus", "18"},
      {"festival", "19"},    {"festivalplus", "20"}, {"buddies", "21"},
      {"buddiesplus", "22"}, {"prism", "23"},        {"prismplus", "24"},
      {"circle", "25"},
  };

  const std::string key = normalize_version_name_key(std::string(version_name));
  if (key.empty()) {
    return {};
  }
  const auto it = kVersionIdByName.find(key);
  if (it == kVersionIdByName.end()) {
    return {};
  }
  return it->second;
}

bool is_decimal_number(std::string_view value) {
  return !value.empty() &&
         std::all_of(value.begin(), value.end(),
                     [](unsigned char c) { return std::isdigit(c) != 0; });
}

std::vector<std::string> split_filter_expression(std::string_view expression) {
  std::vector<std::string> tokens;
  std::string current;
  int bracket_depth = 0;
  int brace_depth = 0;
  int paren_depth = 0;
  bool escaped = false;

  for (const char c : expression) {
    if (escaped) {
      current.push_back(c);
      escaped = false;
      continue;
    }
    if (c == '\\') {
      current.push_back(c);
      escaped = true;
      continue;
    }

    if (c == '[') {
      ++bracket_depth;
    } else if (c == ']' && bracket_depth > 0) {
      --bracket_depth;
    } else if (c == '{' && bracket_depth == 0) {
      ++brace_depth;
    } else if (c == '}' && brace_depth > 0 && bracket_depth == 0) {
      --brace_depth;
    } else if (c == '(' && bracket_depth == 0) {
      ++paren_depth;
    } else if (c == ')' && paren_depth > 0 && bracket_depth == 0) {
      --paren_depth;
    }

    if (c == ',' && bracket_depth == 0 && brace_depth == 0 &&
        paren_depth == 0) {
      tokens.push_back(trim(current));
      current.clear();
      continue;
    }

    current.push_back(c);
  }

  tokens.push_back(trim(current));
  return tokens;
}

std::vector<std::string>
normalize_filter_tokens(const std::vector<std::string> &inputs) {
  std::vector<std::string> tokens;
  for (const auto &entry : inputs) {
    const auto split_tokens = split_filter_expression(entry);
    tokens.insert(tokens.end(), split_tokens.begin(), split_tokens.end());
  }
  return tokens;
}

} // namespace

std::pair<std::string, std::string>
complete_version_fields(std::string version_id, std::string version) {
  version_id = trim(version_id);
  version = trim(version);

  if ((version.empty() || version == "Unknown") && !version_id.empty() &&
      version_id != "0") {
    const std::string inferred = version_name_from_id(version_id);
    if (!inferred.empty()) {
      version = inferred;
    }
  }

  if ((version_id.empty() || version_id == "0") && !version.empty() &&
      version != "Unknown") {
    const std::string inferred = version_id_from_name(version);
    if (!inferred.empty()) {
      version_id = inferred;
    }
  }

  return {version_id, version};
}

std::string normalize_export_version_display(std::string version) {
  const std::string key = normalize_version_name_key(version);
  if (key == "maimadx" || key == "deluxe") {
    return "DELUXE";
  }
  if (key == "maimadxplus" || key == "deluxeplus") {
    return "DELUXE PLUS";
  }
  return version;
}

NumericFilterSet compile_music_id_filters(const AssetsOptions &options) {
  std::vector<std::string> filter_inputs = options.target_music_filters;
  if (options.target_music_id.has_value() &&
      !options.target_music_id->empty()) {
    filter_inputs.push_back(*options.target_music_id);
  }

  NumericFilterSet filters;
  filters.provided = !filter_inputs.empty();
  for (const auto &token : normalize_filter_tokens(filter_inputs)) {
    if (token.empty()) {
      throw std::runtime_error("music id filter contains empty item");
    }
    filters.raw_tokens.push_back(token);
    if (is_decimal_number(token)) {
      const std::string padded = pad_music_id(token, 6);
      if (!is_decimal_number(padded)) {
        throw std::runtime_error("music id must be numeric");
      }
      filters.exact.insert(padded);
      continue;
    }
    try {
      filters.regex.emplace_back(token, std::regex::ECMAScript);
    } catch (const std::regex_error &ex) {
      throw std::runtime_error("invalid music id regex \"" + token +
                               "\": " + ex.what());
    }
  }
  return filters;
}

VersionFilterSet compile_version_filters(const AssetsOptions &options) {
  std::vector<std::string> filter_inputs = options.target_version_filters;
  if (options.target_version.has_value() && !options.target_version->empty()) {
    filter_inputs.push_back(*options.target_version);
  }

  VersionFilterSet filters;
  filters.provided = !filter_inputs.empty();
  for (const auto &token : normalize_filter_tokens(filter_inputs)) {
    if (token.empty()) {
      throw std::runtime_error("version filter contains empty item");
    }
    filters.raw_tokens.push_back(token);
    if (is_decimal_number(token)) {
      const int value = to_int(token, -1);
      if (value < 0) {
        throw std::runtime_error("version id must be >= 0");
      }
      filters.exact_version_ids.insert(std::to_string(value));
      continue;
    }

    const std::string normalized_name = normalize_version_name_key(token);
    if (!normalized_name.empty()) {
      filters.exact_version_names.insert(normalized_name);
    }

    try {
      filters.regex.emplace_back(token,
                                 std::regex::ECMAScript | std::regex::icase);
    } catch (const std::regex_error &ex) {
      throw std::runtime_error("invalid version regex \"" + token +
                               "\": " + ex.what());
    }
  }
  return filters;
}

bool matches_version_filter(const TrackInfo &info,
                            const VersionFilterSet &filters) {
  if (!filters.active()) {
    return true;
  }

  const auto completed = complete_version_fields(info.version_id, info.version);
  const std::string version_id =
      trim(completed.first.empty() ? info.version_id : completed.first);
  const std::string version_name =
      trim(completed.second.empty() ? info.version : completed.second);
  const std::string export_version_name =
      trim(normalize_export_version_display(version_name));
  const std::string normalized_version_name =
      normalize_version_name_key(version_name);
  const std::string normalized_export_version_name =
      normalize_version_name_key(export_version_name);

  if (!version_id.empty() && filters.exact_version_ids.find(version_id) !=
                                 filters.exact_version_ids.end()) {
    return true;
  }
  if ((!normalized_version_name.empty() &&
       filters.exact_version_names.find(normalized_version_name) !=
           filters.exact_version_names.end()) ||
      (!normalized_export_version_name.empty() &&
       filters.exact_version_names.find(normalized_export_version_name) !=
           filters.exact_version_names.end())) {
    return true;
  }

  const auto regex_matches = [&](const std::string &candidate) {
    if (candidate.empty()) {
      return false;
    }
    return std::any_of(
        filters.regex.begin(), filters.regex.end(),
        [&](const std::regex &re) { return std::regex_match(candidate, re); });
  };

  return regex_matches(version_id) || regex_matches(version_name) ||
         regex_matches(export_version_name) ||
         regex_matches(normalized_version_name) ||
         regex_matches(normalized_export_version_name);
}

NumericFilterSet compile_difficulty_filters(const AssetsOptions &options) {
  std::vector<std::string> filter_inputs = options.target_difficulty_filters;
  if (options.target_difficulty.has_value()) {
    filter_inputs.push_back(std::to_string(*options.target_difficulty));
  }

  NumericFilterSet filters;
  filters.provided = !filter_inputs.empty();
  for (const auto &token : normalize_filter_tokens(filter_inputs)) {
    if (token.empty()) {
      throw std::runtime_error("difficulty filter contains empty item");
    }
    filters.raw_tokens.push_back(token);
    if (is_decimal_number(token)) {
      const int value = to_int(token, -1);
      if (value < 1 || value > 7) {
        throw std::runtime_error("difficulty must be in range 1..7");
      }
      filters.exact.insert(std::to_string(value));
      continue;
    }
    try {
      filters.regex.emplace_back(token, std::regex::ECMAScript);
    } catch (const std::regex_error &ex) {
      throw std::runtime_error("invalid difficulty regex \"" + token +
                               "\": " + ex.what());
    }
  }
  return filters;
}

} // namespace maiconv
