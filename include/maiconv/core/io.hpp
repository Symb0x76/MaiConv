#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace maiconv {

std::string read_text_file(const std::filesystem::path &path);
std::vector<std::string> read_lines(const std::filesystem::path &path);
void write_text_file(const std::filesystem::path &path,
                     std::string_view content);

std::vector<std::string> split(std::string_view value, char delimiter);
std::string trim(std::string_view value);
std::string lower(std::string_view value);

int to_int(std::string_view value, int fallback = 0);
double to_double(std::string_view value, double fallback = 0.0);

std::string pad_music_id(const std::string &id, std::size_t width = 6);
std::string sanitize_folder_name(const std::string &name);
std::filesystem::path path_from_utf8(std::string_view utf8);
std::filesystem::path append_utf8_path(const std::filesystem::path &base,
                                       std::string_view leaf_utf8);

bool file_non_empty(const std::filesystem::path &path);

} // namespace maiconv
