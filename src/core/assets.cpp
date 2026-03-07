#include "maiconv/core/assets.hpp"

#include "maiconv/core/chart.hpp"
#include "maiconv/core/io.hpp"
#include "maiconv/core/ma2.hpp"
#include "maiconv/core/media.hpp"
#include "maiconv/core/simai.hpp"

#include <tinyxml2.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
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

int to_int(const std::string& s, int fallback = 0) {
  try {
    return std::stoi(s);
  } catch (...) {
    return fallback;
  }
}

struct TrackInfo {
  std::string id;
  std::string name;
  std::string sort_name;
  std::string genre;
  std::string version;
  std::string composer;
  std::string bpm;
  bool is_dx = false;
  bool is_utage = false;
};

std::string element_text(tinyxml2::XMLElement* element) {
  if (element == nullptr) {
    return "";
  }
  if (auto* str = element->FirstChildElement("str")) {
    return str->GetText() == nullptr ? "" : str->GetText();
  }
  return element->GetText() == nullptr ? "" : element->GetText();
}

tinyxml2::XMLElement* find_first_element_by_name(tinyxml2::XMLNode* node, const char* name) {
  if (node == nullptr) {
    return nullptr;
  }
  for (auto* elem = node->FirstChildElement(); elem != nullptr; elem = elem->NextSiblingElement()) {
    if (std::string(elem->Name()) == name) {
      return elem;
    }
    if (auto* nested = find_first_element_by_name(elem, name)) {
      return nested;
    }
  }
  return nullptr;
}

TrackInfo parse_track_info(const std::filesystem::path& music_xml, const std::string& fallback_id) {
  TrackInfo info;
  info.id = fallback_id;
  info.name = fallback_id;
  info.sort_name = fallback_id;
  info.genre = "Unknown";
  info.version = "Unknown";
  info.composer = "Unknown";
  info.bpm = "120";

  tinyxml2::XMLDocument doc;
  if (doc.LoadFile(music_xml.string().c_str()) != tinyxml2::XML_SUCCESS) {
    return info;
  }

  auto* root = doc.RootElement();
  if (root == nullptr) {
    return info;
  }

  auto* name_element = find_first_element_by_name(root, "name");
  if (name_element != nullptr) {
    info.name = element_text(name_element);
  }
  if (auto* sort_name = find_first_element_by_name(root, "sortName")) {
    info.sort_name = element_text(sort_name);
  } else {
    info.sort_name = info.name;
  }
  if (auto* genre = find_first_element_by_name(root, "genreName")) {
    info.genre = element_text(genre);
  } else if (auto* genre = find_first_element_by_name(root, "genre")) {
    info.genre = element_text(genre);
  }
  if (auto* version = find_first_element_by_name(root, "version") ) {
    info.version = element_text(version);
  }
  if (auto* composer = find_first_element_by_name(root, "artistName")) {
    info.composer = element_text(composer);
  } else if (auto* composer = find_first_element_by_name(root, "artist")) {
    info.composer = element_text(composer);
  }
  if (auto* bpm = find_first_element_by_name(root, "bpm")) {
    info.bpm = element_text(bpm);
  }

  std::string candidate_id;
  if (name_element != nullptr) {
    if (auto* nested_id = name_element->FirstChildElement("id")) {
      candidate_id = trim(element_text(nested_id));
    }
  }
  if (!candidate_id.empty() &&
      std::all_of(candidate_id.begin(), candidate_id.end(), [](unsigned char c) { return std::isdigit(c) != 0; })) {
    info.id = pad_music_id(candidate_id, 6);
  }

  info.id = pad_music_id(info.id, 6);
  info.is_dx = info.id.size() >= 2 && info.id[1] == '1';
  info.is_utage = info.id.size() >= 1 && info.id[0] == '1';

  return info;
}

std::optional<int> parse_ma2_difficulty(const std::filesystem::path& ma2_file) {
  static const std::regex suffix_re(R"(_(\d{2})\.ma2$)", std::regex::icase);
  std::smatch match;
  const std::string name = ma2_file.filename().string();
  if (!std::regex_search(name, match, suffix_re)) {
    return std::nullopt;
  }
  return std::stoi(match[1].str());
}

int infer_inote_index(const std::filesystem::path& ma2_file) {
  const auto parsed = parse_ma2_difficulty(ma2_file);
  if (!parsed.has_value()) {
    return 1;
  }
  if (*parsed >= 1 && *parsed <= 7) {
    return *parsed;
  }
  if (*parsed == 0) {
    return 1;
  }
  return 1;
}

std::filesystem::path find_asset_candidate(const std::filesystem::path& base, const std::vector<std::string>& names) {
  for (const auto& name : names) {
    const auto candidate = base / name;
    if (std::filesystem::exists(candidate)) {
      return candidate;
    }
  }
  return {};
}

std::vector<std::filesystem::path> detect_asset_bases(const std::vector<std::filesystem::path>& source_roots,
                                                      const std::string& folder_name) {
  std::vector<std::filesystem::path> bases;
  for (const auto& root : source_roots) {
    const auto candidate = root / folder_name;
    if (std::filesystem::exists(candidate) && std::filesystem::is_directory(candidate)) {
      bases.push_back(candidate);
    }
  }
  std::sort(bases.begin(), bases.end());
  bases.erase(std::unique(bases.begin(), bases.end()), bases.end());
  return bases;
}

std::filesystem::path find_asset_candidate_in_bases(const std::vector<std::filesystem::path>& bases,
                                                    const std::vector<std::string>& names) {
  for (const auto& base : bases) {
    const auto found = find_asset_candidate(base, names);
    if (!found.empty()) {
      return found;
    }
  }
  return {};
}

std::string path_to_utf8(const std::filesystem::path& path) {
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

std::string category_name_for_layout(const TrackInfo& info, AssetsExportLayout layout) {
  switch (layout) {
    case AssetsExportLayout::Flat:
      return "";
    case AssetsExportLayout::Genre:
      return info.genre.empty() ? "Unknown" : info.genre;
    case AssetsExportLayout::Version:
      return info.version.empty() ? "Unknown" : info.version;
  }
  return "";
}

std::string json_escape(const std::string& value) {
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

void write_log_files(const std::filesystem::path& output,
                     const std::map<int, std::string>& compiled,
                     const std::vector<std::string>& warnings,
                     bool write_json) {
  std::ostringstream text;
  text << "Total music compiled: " << compiled.size() << "\n";
  int idx = 1;
  for (const auto& [id, name] : compiled) {
    text << "[" << idx++ << "]: " << id << " " << name << "\n";
  }
  if (!warnings.empty()) {
    text << "\nWarnings:\n";
    for (const auto& warning : warnings) {
      text << warning << "\n";
    }
  }
  write_text_file(output / "_log.txt", text.str());

  if (write_json) {
    std::ostringstream json;
    json << "{\n";
    std::size_t i = 0;
    for (const auto& [id, name] : compiled) {
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

void write_collections(const std::filesystem::path& output,
                       const std::map<std::string, std::vector<std::string>>& groups) {
  for (const auto& [name, ids] : groups) {
    const auto folder = append_utf8_path(output / "collections", sanitize_folder_name(name.empty() ? "default" : name));
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

bool zip_and_remove(const std::filesystem::path& folder) {
  (void)folder;
  return false;
}

std::string compose_simai_document(const TrackInfo& info,
                                   const std::map<int, std::string>& inotes,
                                   bool strict_decimal) {
  (void)strict_decimal;
  std::ostringstream out;
  out << "&title=" << info.name << "\n";
  out << "&wholebpm=" << info.bpm << "\n";
  out << "&artist=" << info.composer << "\n";
  out << "&shortid=" << info.id << "\n";
  out << "&genre=" << info.genre << "\n";
  out << "&version=" << info.version << "\n";
  out << "&cabinet=" << (info.is_dx ? "DX" : "SD") << "\n";
  out << "&ChartConvertTool=maiconv\n";
  out << "&ChartConverter=maiconv\n\n";

  for (const auto& [diff, body] : inotes) {
    out << "&inote_" << diff << "=\n";
    out << body << "\n\n";
  }
  return out.str();
}

}  // namespace

int run_compile_assets(const AssetsOptions& options) {
  try {
    if (options.streaming_assets_path.empty() || options.output_path.empty()) {
      throw std::runtime_error("input path and output path are required");
    }
    if (!std::filesystem::exists(options.streaming_assets_path)) {
      throw std::runtime_error("Input folder not found: " + options.streaming_assets_path.string());
    }

    std::filesystem::create_directories(options.output_path);

    std::optional<std::string> target_music_id;
    if (options.target_music_id.has_value() && !options.target_music_id->empty()) {
      const std::string padded = pad_music_id(*options.target_music_id, 6);
      if (!std::all_of(padded.begin(), padded.end(), [](unsigned char c) { return std::isdigit(c) != 0; })) {
        throw std::runtime_error("music id must be numeric");
      }
      target_music_id = padded;
    }

    std::optional<int> target_difficulty = options.target_difficulty;
    if (target_difficulty.has_value()) {
      if (!target_music_id.has_value()) {
        throw std::runtime_error("difficulty requires music id");
      }
      if (*target_difficulty < 0 || *target_difficulty > 7) {
        throw std::runtime_error("difficulty must be in range 0..7");
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
    for (const auto& entry : std::filesystem::directory_iterator(options.streaming_assets_path)) {
      if (entry.is_directory()) {
        source_roots.push_back(entry.path());
      }
    }
    std::sort(source_roots.begin(), source_roots.end());
    source_roots.erase(std::unique(source_roots.begin(), source_roots.end()), source_roots.end());

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

    std::vector<std::filesystem::path> folders;
    for (const auto& root : source_roots) {
      const auto music_root = root / "music";
      if (!std::filesystem::exists(music_root) || !std::filesystem::is_directory(music_root)) {
        continue;
      }
      for (const auto& entry : std::filesystem::directory_iterator(music_root)) {
        if (entry.is_directory()) {
          folders.push_back(entry.path());
        }
      }
    }
    std::sort(folders.begin(), folders.end());
    if (folders.empty()) {
      throw std::runtime_error("No music folders found under input path: " + options.streaming_assets_path.string());
    }

    for (const auto& folder : folders) {
      const std::string folder_name = folder.filename().string();
      std::string fallback_id;
      if (folder_name.rfind("music", 0) == 0) {
        fallback_id = folder_name.substr(5);
      } else {
        fallback_id = folder_name;
      }
      fallback_id = pad_music_id(fallback_id, 6);

      const auto xml_path = folder / "Music.xml";
      TrackInfo info = std::filesystem::exists(xml_path) ? parse_track_info(xml_path, fallback_id)
                                                         : TrackInfo{fallback_id, fallback_id, fallback_id,
                                                                     "Unknown", "Unknown", "Unknown", "120", false,
                                                                     false};

      if (target_music_id.has_value() && info.id != *target_music_id) {
        continue;
      }
      if (target_music_id.has_value()) {
        matched_music_id = true;
      }

      std::vector<std::filesystem::path> ma2_files;
      for (const auto& entry : std::filesystem::directory_iterator(folder)) {
        if (!entry.is_regular_file() || lower(entry.path().extension().string()) != ".ma2") {
          continue;
        }
        if (target_difficulty.has_value()) {
          const auto diff = parse_ma2_difficulty(entry.path());
          if (!diff.has_value() || *diff != *target_difficulty) {
            continue;
          }
        }
        ma2_files.push_back(entry.path());
      }
      std::sort(ma2_files.begin(), ma2_files.end());
      if (ma2_files.empty()) {
        continue;
      }
      if (target_difficulty.has_value()) {
        matched_difficulty = true;
      }

      const std::string category = category_name_for_layout(info, options.export_layout);
      std::filesystem::path category_folder = options.output_path;
      if (!category.empty()) {
        category_folder = append_utf8_path(category_folder, sanitize_folder_name(category));
      }
      std::filesystem::create_directories(category_folder);

      const std::string display_name = info.name.empty() ? info.sort_name : info.name;
      std::string folder_stem = options.music_id_folder_name
                                    ? info.id
                                    : (info.id + "_" + sanitize_folder_name(display_name));
      std::filesystem::path track_output = append_utf8_path(category_folder, folder_stem);
      std::filesystem::create_directories(track_output);

      std::map<int, std::string> inotes;
      for (const auto& ma2_file : ma2_files) {
        const auto chart = parser.parse(tokenizer.tokenize_file(ma2_file));
        Chart transformed = chart;
        if (options.rotate.has_value()) {
          transformed.rotate(*options.rotate);
        }
        if (options.shift_ticks != 0) {
          transformed.shift_by_offset(options.shift_ticks);
        }

        if (options.format == ChartFormat::Simai || options.format == ChartFormat::SimaiFes) {
          const int inote = infer_inote_index(ma2_file);
          inotes[inote] = simai_composer.compose_chart(transformed);
        } else {
          const std::string text = ma2_composer.compose(transformed, options.format);
          write_text_file(track_output / "result.ma2", text);
        }
      }

      if (!inotes.empty()) {
        write_text_file(track_output / "maidata.txt", compose_simai_document(info, inotes, options.strict_decimal));
      }

      bool incomplete = false;

      int id_number = to_int(info.id, 0);
      if (id_number < 0) {
        id_number = -id_number;
      }
      const std::string non_dx_id = pad_music_id(std::to_string(id_number % 10000), 6);
      const std::string short_id = non_dx_id.size() >= 2 ? non_dx_id.substr(2) : non_dx_id;

      auto append_unique = [](std::vector<std::string>& values, const std::string& value) {
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
        for (const auto& base_name : audio_bases) {
          compressed_audio_names.push_back(base_name + ".mp3");
          compressed_audio_names.push_back(base_name + ".ogg");
        }

        const auto compressed_audio = find_asset_candidate_in_bases(music_bases, compressed_audio_names);
        if (!compressed_audio.empty()) {
          const std::string ext = lower(compressed_audio.extension().string());
          if (ext == ".mp3") {
            std::filesystem::copy_file(compressed_audio, track_output / "track.mp3",
                                       std::filesystem::copy_options::overwrite_existing);
          } else {
            if (!convert_audio_to_mp3(compressed_audio, track_output / "track.mp3")) {
              warnings.push_back("Audio conversion failed: " + path_to_utf8(compressed_audio) + " -> " + path_to_utf8(track_output / "track.mp3"));
              incomplete = true;
            }
          }
        } else {
          std::vector<std::string> acb_names;
          std::vector<std::string> awb_names;
          for (const auto& base_name : audio_bases) {
            acb_names.push_back(base_name + ".acb");
            awb_names.push_back(base_name + ".awb");
          }

          const auto acb_source = find_asset_candidate_in_bases(music_bases, acb_names);
          const auto awb_source = find_asset_candidate_in_bases(music_bases, awb_names);
          if (!acb_source.empty() && !awb_source.empty()) {
            if (!convert_acb_awb_to_mp3(acb_source, awb_source, track_output / "track.mp3")) {
              warnings.push_back("Audio conversion failed: " + path_to_utf8(acb_source) + " + " + path_to_utf8(awb_source) + " -> " + path_to_utf8(track_output / "track.mp3"));
              incomplete = true;
            }
          } else {
            warnings.push_back("Music missing: " + info.name + " (" + info.id + ")");
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
        for (const auto& stem : jacket_stems) {
          image_names.push_back(stem + ".png");
          image_names.push_back(stem + ".jpg");
          image_names.push_back(stem + ".jpeg");
          image_names.push_back("jacket/" + stem + ".png");
          image_names.push_back("jacket/" + stem + ".jpg");
          image_names.push_back("jacket/" + stem + ".jpeg");
          ab_names.push_back(stem + ".ab");
          ab_names.push_back("jacket/" + stem + ".ab");
        }

        const auto cover_image = find_asset_candidate_in_bases(cover_bases, image_names);
        if (!cover_image.empty()) {
          std::string ext = lower(cover_image.extension().string());
          if (ext.empty()) {
            ext = ".png";
          }
          std::filesystem::copy_file(cover_image, track_output / ("bg" + ext),
                                     std::filesystem::copy_options::overwrite_existing);
        } else {
          const auto cover_ab = find_asset_candidate_in_bases(cover_bases, ab_names);
          if (!cover_ab.empty()) {
            if (!convert_ab_to_png(cover_ab, track_output / "bg.png")) {
              warnings.push_back("Cover conversion failed: " + path_to_utf8(cover_ab) + " -> " + path_to_utf8(track_output / "bg.png"));
              incomplete = true;
            }
          } else {
            warnings.push_back("Cover missing: " + info.name + " (" + info.id + ")");
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
        for (const auto& stem : movie_stems) {
          video_mp4_names.push_back(stem + ".mp4");
          video_dat_names.push_back(stem + ".dat");
          video_usm_names.push_back(stem + ".usm");
        }

        auto video_source = find_asset_candidate_in_bases(video_bases, video_mp4_names);
        if (!video_source.empty()) {
          std::filesystem::copy_file(video_source, track_output / "pv.mp4",
                                     std::filesystem::copy_options::overwrite_existing);
        } else {
          video_source = find_asset_candidate_in_bases(video_bases, video_dat_names);
          if (video_source.empty()) {
            video_source = find_asset_candidate_in_bases(video_bases, video_usm_names);
          }

          if (video_source.empty()) {
            warnings.push_back("Video missing: " + info.name + " (" + info.id + ")");
            incomplete = true;
          } else {
            if (!convert_dat_or_usm_to_mp4(video_source, track_output / "pv.mp4")) {
              warnings.push_back("Video conversion failed: " + path_to_utf8(video_source) + " -> " + path_to_utf8(track_output / "pv.mp4"));
              incomplete = true;
            }
          }
        }
      }
      if (incomplete && !options.ignore_incomplete_assets) {
        throw std::runtime_error("Incomplete assets found. Use --ignore to continue.");
      }

      if (incomplete) {
        auto incomplete_path = track_output;
        incomplete_path += "_Incomplete";
        if (std::filesystem::exists(incomplete_path)) {
          std::filesystem::remove_all(incomplete_path);
        }
        std::filesystem::rename(track_output, incomplete_path);
      } else {
        compiled_tracks[to_int(info.id)] = info.name;
        const std::string collection_name = category.empty() ? "default" : category;
        const std::string dedupe_key = collection_name + "|" + info.id;
        if (collection_seen.insert(dedupe_key).second) {
          collections[collection_name].push_back(info.id);
        }

        if (options.export_zip) {
          if (!zip_and_remove(track_output)) {
            warnings.push_back("Zip export failed: " + path_to_utf8(track_output));
          }
        }
      }
    }

    if (target_music_id.has_value() && !matched_music_id) {
      throw std::runtime_error("Music id not found: " + *target_music_id);
    }
    if (target_difficulty.has_value() && !matched_difficulty) {
      throw std::runtime_error("Difficulty not found for music id: " + *target_music_id +
                               " difficulty=" + std::to_string(*target_difficulty));
    }

    write_log_files(options.output_path, compiled_tracks, warnings, options.log_tracks_json);
    if (options.compile_collections) {
      write_collections(options.output_path, collections);
    }

    return kSuccess;
  } catch (const std::exception& ex) {
    std::cerr << "Program cannot proceed because of following error returned:\n" << ex.what() << "\n";
    return kFailure;
  }
}

}  // namespace maiconv





























