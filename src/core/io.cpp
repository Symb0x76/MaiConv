#include "maiconv/core/io.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace maiconv {
namespace {

std::string path_to_utf8_for_error(const std::filesystem::path &path) {
#if defined(_WIN32)
#if defined(__cpp_char8_t)
  const std::u8string value = path.u8string();
  std::string out;
  out.reserve(value.size());
  for (const char8_t ch : value) {
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

} // namespace

std::string read_text_file(const std::filesystem::path &path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    throw std::runtime_error("cannot open file: " +
                             path_to_utf8_for_error(path));
  }
  std::ostringstream buffer;
  buffer << stream.rdbuf();
  return buffer.str();
}

std::vector<std::string> read_lines(const std::filesystem::path &path) {
  std::ifstream stream(path);
  if (!stream) {
    throw std::runtime_error("cannot open file: " +
                             path_to_utf8_for_error(path));
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

void write_text_file(const std::filesystem::path &path,
                     std::string_view content) {
  if (path.has_parent_path()) {
    std::filesystem::create_directories(path.parent_path());
  }
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  if (!stream) {
    throw std::runtime_error("cannot write file: " +
                             path_to_utf8_for_error(path));
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
  while (start < end &&
         std::isspace(static_cast<unsigned char>(value[start]))) {
    ++start;
  }
  while (end > start &&
         std::isspace(static_cast<unsigned char>(value[end - 1]))) {
    --end;
  }
  return std::string(value.substr(start, end - start));
}

std::string lower(std::string_view value) {
  std::string out(value);
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return out;
}

std::string pad_music_id(const std::string &id, std::size_t width) {
  if (id.size() >= width) {
    return id;
  }
  return std::string(width - id.size(), '0') + id;
}

std::string sanitize_folder_name(const std::string &name) {
  auto append_control_picture = [](std::string &target, unsigned char control) {
    // U+2400..U+241F (Control Pictures), encoded as UTF-8.
    target.push_back(static_cast<char>(0xE2));
    target.push_back(static_cast<char>(0x90));
    target.push_back(static_cast<char>(0x80 + control));
  };

  auto replacement_for_forbidden_char = [](char c) -> const char * {
    switch (c) {
    case '<':
      return "\xEF\xBC\x9C"; // ＜
    case '>':
      return "\xEF\xBC\x9E"; // ＞
    case ':':
      return "\xEF\xBC\x9A"; // ：
    case '"':
      return "\xEF\xBC\x82"; // ＂
    case '/':
      return "\xEF\xBC\x8F"; // ／
    case '\\':
      return "\xEF\xBC\xBC"; // ＼
    case '|':
      return "\xEF\xBD\x9C"; // ｜
    case '?':
      return "\xEF\xBC\x9F"; // ？
    case '*':
      return "\xEF\xBC\x8A"; // ＊
    default:
      return nullptr;
    }
  };

  std::string out;
  out.reserve(name.size() * 3);
  for (char c : name) {
    // Keep export folder names stable across platforms with visually-equivalent
    // symbols instead of lossy ASCII placeholders.
    if (const char *replacement = replacement_for_forbidden_char(c);
        replacement != nullptr) {
      out.append(replacement);
      continue;
    }
    if (static_cast<unsigned char>(c) < 0x20) {
      append_control_picture(out, static_cast<unsigned char>(c));
      continue;
    }
    out.push_back(c);
  }
  if (out.empty()) {
    return "\xEF\xBC\xBF"; // ＿
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

std::filesystem::path append_utf8_path(const std::filesystem::path &base,
                                       std::string_view leaf_utf8) {
  return base / path_from_utf8(leaf_utf8);
}

} // namespace maiconv
