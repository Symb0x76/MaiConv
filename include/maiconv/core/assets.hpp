#pragma once

#include "maiconv/core/format.hpp"

#include <filesystem>
#include <map>
#include <optional>
#include <string>

namespace maiconv {

enum class AssetsExportLayout {
  Flat,
  Genre,
  Version,
};

struct AssetsOptions {
  std::filesystem::path streaming_assets_path;
  std::filesystem::path output_path;
  std::optional<std::filesystem::path> music_path;
  std::optional<std::filesystem::path> cover_path;
  std::optional<std::filesystem::path> video_path;

  std::optional<std::string> target_music_id;
  std::optional<int> target_difficulty;

  ChartFormat format = ChartFormat::Simai;
  AssetsExportLayout export_layout = AssetsExportLayout::Flat;
  std::optional<FlipMethod> rotate;
  int shift_ticks = 0;

  bool strict_decimal = false;
  bool ignore_incomplete_assets = false;
  bool music_id_folder_name = false;
  bool log_tracks_json = false;
  bool export_zip = false;
  bool compile_collections = false;
};

int run_compile_assets(const AssetsOptions& options);

}  // namespace maiconv
