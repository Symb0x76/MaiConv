#include "maiconv/core/zip_util.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace maiconv {
namespace {

constexpr uint32_t kLocalFileHeaderSig = 0x04034B50U;
constexpr uint32_t kCentralDirectorySig = 0x02014B50U;
constexpr uint32_t kEndOfCentralDirSig = 0x06054B50U;

std::array<uint32_t, 256> make_crc32_table() {
  std::array<uint32_t, 256> table{};
  for (uint32_t i = 0; i < 256; ++i) {
    uint32_t value = i;
    for (int bit = 0; bit < 8; ++bit) {
      value = (value & 1U) != 0U ? 0xEDB88320U ^ (value >> 1U) : value >> 1U;
    }
    table[i] = value;
  }
  return table;
}

uint32_t crc32_update(uint32_t crc, const uint8_t *data, std::size_t size) {
  static const std::array<uint32_t, 256> table = make_crc32_table();
  for (std::size_t i = 0; i < size; ++i) {
    crc = table[(crc ^ data[i]) & 0xFFU] ^ (crc >> 8U);
  }
  return crc;
}

void write_u16(std::ofstream &out, uint16_t value) {
  out.put(static_cast<char>(value & 0xFFU));
  out.put(static_cast<char>((value >> 8U) & 0xFFU));
}

void write_u32(std::ofstream &out, uint32_t value) {
  out.put(static_cast<char>(value & 0xFFU));
  out.put(static_cast<char>((value >> 8U) & 0xFFU));
  out.put(static_cast<char>((value >> 16U) & 0xFFU));
  out.put(static_cast<char>((value >> 24U) & 0xFFU));
}

struct Entry {
  std::string name;
  std::uintmax_t size = 0;
  uint32_t crc32 = 0;
  uint32_t local_header_offset = 0;
};

// Writes one file into the zip body (local header + stored data).
// Assumes the target archive does not yet contain an entry with the
// same name.
bool append_stored_entry(std::ofstream &zip,
                         const std::filesystem::path &source_file,
                         const std::string &entry_name, Entry &entry) {
  std::ifstream in(source_file, std::ios::binary);
  if (!in) {
    return false;
  }

  entry.name = entry_name;
  entry.local_header_offset = static_cast<uint32_t>(zip.tellp());
  write_u32(zip, kLocalFileHeaderSig);
  write_u16(zip, 20);   // version needed to extract
  write_u16(zip, 0);    // general purpose flags
  write_u16(zip, 0);    // compression method: store
  write_u16(zip, 0);    // last mod time
  write_u16(zip, 0x21); // last mod date (1980-01-01)
  write_u32(zip, 0);    // crc32, patched below
  write_u32(zip, 0);    // compressed size, patched below
  write_u32(zip, 0);    // uncompressed size, patched below
  write_u16(zip, static_cast<uint16_t>(entry_name.size()));
  write_u16(zip, 0); // extra field length
  zip.write(entry_name.data(), static_cast<std::streamsize>(entry_name.size()));

  constexpr std::size_t kBufferSize = 1U << 16U;
  std::vector<uint8_t> buffer(kBufferSize);
  uint32_t crc = 0xFFFFFFFFU;
  std::uintmax_t total = 0;
  while (in) {
    in.read(reinterpret_cast<char *>(buffer.data()),
            static_cast<std::streamsize>(buffer.size()));
    const std::streamsize read = in.gcount();
    if (read > 0) {
      crc = crc32_update(crc, buffer.data(), static_cast<std::size_t>(read));
      total += static_cast<std::uintmax_t>(read);
      zip.write(reinterpret_cast<const char *>(buffer.data()), read);
    }
  }
  if (!zip) {
    return false;
  }

  entry.size = total;
  entry.crc32 = crc ^ 0xFFFFFFFFU;

  const std::streampos end = zip.tellp();
  zip.seekp(entry.local_header_offset + 14);
  write_u32(zip, entry.crc32);
  write_u32(zip, static_cast<uint32_t>(total));
  write_u32(zip, static_cast<uint32_t>(total));
  zip.seekp(end);
  return static_cast<bool>(zip);
}

} // namespace

bool zip_folder_and_remove(const std::filesystem::path &folder) {
  if (!std::filesystem::is_directory(folder)) {
    return false;
  }

  const std::filesystem::path zip_path = folder.string() + ".zip";
  std::error_code ec;
  if (std::filesystem::exists(zip_path, ec)) {
    return false;
  }
  std::ofstream zip(zip_path, std::ios::binary | std::ios::trunc);
  if (!zip) {
    return false;
  }

  std::vector<Entry> entries;
  for (const auto &entry :
       std::filesystem::recursive_directory_iterator(folder, ec)) {
    if (ec || !entry.is_regular_file()) {
      continue;
    }
    std::string name = entry.path().lexically_relative(folder).generic_string();
    Entry stored;
    if (!append_stored_entry(zip, entry.path(), name, stored)) {
      return false;
    }
    entries.push_back(std::move(stored));
  }
  if (ec) {
    return false;
  }

  const uint32_t central_dir_offset = static_cast<uint32_t>(zip.tellp());
  for (const auto &entry : entries) {
    write_u32(zip, kCentralDirectorySig);
    write_u16(zip, 20);   // version made by
    write_u16(zip, 20);   // version needed to extract
    write_u16(zip, 0);    // general purpose flags
    write_u16(zip, 0);    // compression method: store
    write_u16(zip, 0);    // last mod time
    write_u16(zip, 0x21); // last mod date
    write_u32(zip, entry.crc32);
    write_u32(zip, static_cast<uint32_t>(entry.size));
    write_u32(zip, static_cast<uint32_t>(entry.size));
    write_u16(zip, static_cast<uint16_t>(entry.name.size()));
    write_u16(zip, 0); // extra field length
    write_u16(zip, 0); // comment length
    write_u16(zip, 0); // disk number start
    write_u16(zip, 0); // internal attributes
    write_u32(zip, 0); // external attributes
    write_u32(zip, entry.local_header_offset);
    zip.write(entry.name.data(),
              static_cast<std::streamsize>(entry.name.size()));
  }
  const uint32_t central_dir_size =
      static_cast<uint32_t>(zip.tellp()) - central_dir_offset;

  write_u32(zip, kEndOfCentralDirSig);
  write_u16(zip, 0); // disk number
  write_u16(zip, 0); // disk with central dir
  write_u16(zip, static_cast<uint16_t>(entries.size()));
  write_u16(zip, static_cast<uint16_t>(entries.size()));
  write_u32(zip, central_dir_size);
  write_u32(zip, central_dir_offset);
  write_u16(zip, 0); // comment length
  zip.flush();
  if (!zip) {
    return false;
  }

  std::filesystem::remove_all(folder, ec);
  return !ec;
}

} // namespace maiconv
