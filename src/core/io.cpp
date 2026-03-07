#include "maiconv/core/io.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace maiconv {

std::string read_text_file(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    throw std::runtime_error("cannot open file: " + path.string());
  }
  std::ostringstream buffer;
  buffer << stream.rdbuf();
  return buffer.str();
}

std::vector<std::string> read_lines(const std::filesystem::path& path) {
  std::ifstream stream(path);
  if (!stream) {
    throw std::runtime_error("cannot open file: " + path.string());
  }
  std::vector<std::string> lines;
  for (std::string line; std::getline(stream, line);) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    lines.push_back(line);
  }
  return lines;
}

void write_text_file(const std::filesystem::path& path, std::string_view content) {
  if (path.has_parent_path()) {
    std::filesystem::create_directories(path.parent_path());
  }
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  if (!stream) {
    throw std::runtime_error("cannot write file: " + path.string());
  }
  stream << content;
}

std::vector<std::string> split(std::string_view value, char delimiter) {
  std::vector<std::string> out;
  std::string current;
  for (char c : value) {
    if (c == delimiter) {
      out.push_back(current);
      current.clear();
    } else {
      current.push_back(c);
    }
  }
  out.push_back(current);
  return out;
}

std::string trim(std::string_view value) {
  std::size_t start = 0;
  std::size_t end = value.size();
  while (start < end && std::isspace(static_cast<unsigned char>(value[start]))) {
    ++start;
  }
  while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
    --end;
  }
  return std::string(value.substr(start, end - start));
}

std::string lower(std::string_view value) {
  std::string out(value);
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return out;
}

std::string pad_music_id(const std::string& id, std::size_t width) {
  if (id.size() >= width) {
    return id;
  }
  return std::string(width - id.size(), '0') + id;
}

std::string sanitize_folder_name(const std::string& name) {
  std::string out;
  out.reserve(name.size());
  for (char c : name) {
#if defined(_WIN32)
    if (c == '<' || c == '>' || c == ':' || c == '"' || c == '/' || c == '\\' || c == '|' || c == '?' ||
        c == '*') {
      out.push_back('_');
      continue;
    }
#endif
    if (static_cast<unsigned char>(c) < 0x20) {
      out.push_back('_');
      continue;
    }
    out.push_back(c);
  }
  if (out.empty()) {
    return "_";
  }
  return out;
}

std::filesystem::path path_from_utf8(std::string_view utf8) {
#if defined(_WIN32)
#if defined(__cpp_char8_t)
  std::u8string value;
  value.reserve(utf8.size());
  for (const char ch : utf8) {
    value.push_back(static_cast<char8_t>(static_cast<unsigned char>(ch)));
  }
  return std::filesystem::path(value);
#else
  return std::filesystem::u8path(std::string(utf8));
#endif
#else
  return std::filesystem::path(std::string(utf8));
#endif
}

std::filesystem::path append_utf8_path(const std::filesystem::path& base, std::string_view leaf_utf8) {
  return base / path_from_utf8(leaf_utf8);
}

}  // namespace maiconv


