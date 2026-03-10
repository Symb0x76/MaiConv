#include "maiconv/core/media.hpp"

#include "maiconv/core/io.hpp"

namespace maiconv {
  bool extract_unity_texture_bundle_to_png(const std::filesystem::path& ab_file,
    const std::filesystem::path& png_file);
}

extern "C" {
#include "layer3.h"
#include "libvgmstream.h"
}

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace maiconv {
  namespace {

    constexpr std::array<uint8_t, 16> kAcbStubMagic = {
      static_cast<uint8_t>('M'), static_cast<uint8_t>('A'), static_cast<uint8_t>('I'), static_cast<uint8_t>('C'),
      static_cast<uint8_t>('O'), static_cast<uint8_t>('N'), static_cast<uint8_t>('V'), static_cast<uint8_t>('_'),
      static_cast<uint8_t>('A'), static_cast<uint8_t>('C'), static_cast<uint8_t>('B'), static_cast<uint8_t>('_'),
      static_cast<uint8_t>('S'), static_cast<uint8_t>('T'), static_cast<uint8_t>('U'), static_cast<uint8_t>('B') };

    void write_u32_le(std::ofstream& out, uint32_t value) {
      const std::array<uint8_t, 4> bytes = {
        static_cast<uint8_t>(value & 0xFFU),
        static_cast<uint8_t>((value >> 8U) & 0xFFU),
        static_cast<uint8_t>((value >> 16U) & 0xFFU),
        static_cast<uint8_t>((value >> 24U) & 0xFFU) };
      out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    void write_u64_le(std::ofstream& out, uint64_t value) {
      const std::array<uint8_t, 8> bytes = {
        static_cast<uint8_t>(value & 0xFFU),
        static_cast<uint8_t>((value >> 8U) & 0xFFU),
        static_cast<uint8_t>((value >> 16U) & 0xFFU),
        static_cast<uint8_t>((value >> 24U) & 0xFFU),
        static_cast<uint8_t>((value >> 32U) & 0xFFU),
        static_cast<uint8_t>((value >> 40U) & 0xFFU),
        static_cast<uint8_t>((value >> 48U) & 0xFFU),
        static_cast<uint8_t>((value >> 56U) & 0xFFU) };
      out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    bool is_mp3_like_file(const std::filesystem::path& path) {
      std::ifstream in(path, std::ios::binary);
      if (!in) {
        return false;
      }

      std::array<uint8_t, 3> head{};
      in.read(reinterpret_cast<char*>(head.data()), static_cast<std::streamsize>(head.size()));
      if (in.gcount() < 2) {
        return false;
      }

      if (in.gcount() == 3 && head[0] == static_cast<uint8_t>('I') && head[1] == static_cast<uint8_t>('D') &&
        head[2] == static_cast<uint8_t>('3')) {
        return true;
      }

      return head[0] == 0xFFU && (head[1] & 0xE0U) == 0xE0U;
    }

    bool read_acb_stub_sidecar_awb_name(const std::filesystem::path& acb,
      std::string& awb_name_out,
      uint64_t& awb_size_out) {
      std::ifstream in(acb, std::ios::binary);
      if (!in) {
        return false;
      }

      std::array<uint8_t, 16> magic{};
      in.read(reinterpret_cast<char*>(magic.data()), static_cast<std::streamsize>(magic.size()));
      if (in.gcount() != static_cast<std::streamsize>(magic.size()) || magic != kAcbStubMagic) {
        return false;
      }

      std::array<uint8_t, 4> u32{};
      in.read(reinterpret_cast<char*>(u32.data()), static_cast<std::streamsize>(u32.size()));
      if (in.gcount() != static_cast<std::streamsize>(u32.size())) {
        return false;
      }
      const uint32_t version = static_cast<uint32_t>(u32[0]) |
        (static_cast<uint32_t>(u32[1]) << 8U) |
        (static_cast<uint32_t>(u32[2]) << 16U) |
        (static_cast<uint32_t>(u32[3]) << 24U);
      if (version != 1U) {
        return false;
      }

      std::array<uint8_t, 8> u64{};
      in.read(reinterpret_cast<char*>(u64.data()), static_cast<std::streamsize>(u64.size()));
      if (in.gcount() != static_cast<std::streamsize>(u64.size())) {
        return false;
      }
      awb_size_out = static_cast<uint64_t>(u64[0]) |
        (static_cast<uint64_t>(u64[1]) << 8U) |
        (static_cast<uint64_t>(u64[2]) << 16U) |
        (static_cast<uint64_t>(u64[3]) << 24U) |
        (static_cast<uint64_t>(u64[4]) << 32U) |
        (static_cast<uint64_t>(u64[5]) << 40U) |
        (static_cast<uint64_t>(u64[6]) << 48U) |
        (static_cast<uint64_t>(u64[7]) << 56U);

      in.read(reinterpret_cast<char*>(u32.data()), static_cast<std::streamsize>(u32.size()));
      if (in.gcount() != static_cast<std::streamsize>(u32.size())) {
        return false;
      }
      const uint32_t name_size = static_cast<uint32_t>(u32[0]) |
        (static_cast<uint32_t>(u32[1]) << 8U) |
        (static_cast<uint32_t>(u32[2]) << 16U) |
        (static_cast<uint32_t>(u32[3]) << 24U);
      if (name_size == 0U || name_size > 1024U) {
        return false;
      }

      std::string awb_name(name_size, '\0');
      in.read(awb_name.data(), static_cast<std::streamsize>(awb_name.size()));
      if (in.gcount() != static_cast<std::streamsize>(awb_name.size())) {
        return false;
      }

      awb_name_out = std::move(awb_name);
      return true;
    }

    uint32_t read_u32_be(const uint8_t* p) {
      return (static_cast<uint32_t>(p[0]) << 24U) |
        (static_cast<uint32_t>(p[1]) << 16U) |
        (static_cast<uint32_t>(p[2]) << 8U) |
        static_cast<uint32_t>(p[3]);
    }

    uint16_t read_u16_le(const uint8_t* p) {
      return static_cast<uint16_t>(p[0]) |
        static_cast<uint16_t>(static_cast<uint16_t>(p[1]) << 8U);
    }

    uint16_t read_u16_be(const uint8_t* p) {
      return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8U) |
        static_cast<uint16_t>(p[1]));
    }

    bool file_non_empty(const std::filesystem::path& path) {
      return std::filesystem::exists(path) &&
        std::filesystem::is_regular_file(path) &&
        std::filesystem::file_size(path) > 0;
    }

    std::filesystem::path make_temp_work_dir() {
      static std::atomic<unsigned long long> counter{ 0 };
      const auto now = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch())
        .count();
      const auto id = counter.fetch_add(1, std::memory_order_relaxed);
      const auto dir = std::filesystem::temp_directory_path() /
        ("maiconv_media_" + std::to_string(now) + "_" + std::to_string(id));
      std::filesystem::create_directories(dir);
      return dir;
    }

    bool read_exact(std::istream& in, uint8_t* out, std::size_t size) {
      if (size == 0) {
        return true;
      }
      in.read(reinterpret_cast<char*>(out), static_cast<std::streamsize>(size));
      return static_cast<std::size_t>(in.gcount()) == size;
    }

    template <std::size_t N>
    bool starts_with(const std::vector<uint8_t>& data, const std::array<uint8_t, N>& sig) {
      return data.size() >= sig.size() && std::equal(sig.begin(), sig.end(), data.begin());
    }

    std::optional<int> pick_supported_bitrate(int sample_rate) {
      constexpr std::array<int, 11> kCandidates = { 192, 160, 128, 112, 96, 80, 64, 56, 48, 40, 32 };
      for (const int br : kCandidates) {
        if (shine_check_config(sample_rate, br) >= 0) {
          return br;
        }
      }
      return std::nullopt;
    }

    shine_t create_shine_encoder(int sample_rate, int channels, int* samples_per_pass) {
      if (channels < 1 || channels > 2 || samples_per_pass == nullptr) {
        return nullptr;
      }

      const auto bitrate = pick_supported_bitrate(sample_rate);
      if (!bitrate.has_value()) {
        return nullptr;
      }

      shine_config_t cfg{};
      shine_set_config_mpeg_defaults(&cfg.mpeg);
      cfg.wave.samplerate = sample_rate;
      cfg.wave.channels = channels == 1 ? PCM_MONO : PCM_STEREO;
      cfg.mpeg.bitr = *bitrate;
      cfg.mpeg.mode = channels == 1 ? MONO : STEREO;

      shine_t enc = shine_initialise(&cfg);
      if (enc == nullptr) {
        return nullptr;
      }

      *samples_per_pass = shine_samples_per_pass(enc);
      if (*samples_per_pass <= 0) {
        shine_close(enc);
        return nullptr;
      }

      return enc;
    }

    bool decode_vgmstream_to_mp3_subsong(const std::filesystem::path& input,
      int subsong,
      const std::filesystem::path& target_mp3) {
      if (!file_non_empty(input)) {
        return false;
      }

      libstreamfile_t* sf = libstreamfile_open_from_stdio(input.string().c_str());
      if (sf == nullptr) {
        return false;
      }

      libvgmstream_t* lib = libvgmstream_init();
      if (lib == nullptr) {
        libstreamfile_close(sf);
        return false;
      }

      libvgmstream_config_t cfg{};
      cfg.ignore_loop = true;
      cfg.force_sfmt = LIBVGMSTREAM_SFMT_PCM16;
      cfg.auto_downmix_channels = 2;
      libvgmstream_setup(lib, &cfg);

      const int open_err = libvgmstream_open_stream(lib, sf, subsong);
      libstreamfile_close(sf);
      if (open_err < 0) {
        libvgmstream_free(lib);
        return false;
      }

      const int channels = lib->format != nullptr ? lib->format->channels : 0;
      const int sample_rate = lib->format != nullptr ? lib->format->sample_rate : 0;
      if (channels < 1 || channels > 2 || sample_rate <= 0) {
        libvgmstream_free(lib);
        return false;
      }

      int samples_per_pass = 0;
      shine_t enc = create_shine_encoder(sample_rate, channels, &samples_per_pass);
      if (enc == nullptr) {
        libvgmstream_free(lib);
        return false;
      }

      if (!target_mp3.parent_path().empty()) {
        std::filesystem::create_directories(target_mp3.parent_path());
      }
      std::ofstream out(target_mp3, std::ios::binary | std::ios::trunc);
      if (!out) {
        shine_close(enc);
        libvgmstream_free(lib);
        return false;
      }

      std::vector<int16_t> pcm(
        static_cast<std::size_t>(samples_per_pass) * static_cast<std::size_t>(channels),
        0);

      while (lib->decoder != nullptr && !lib->decoder->done) {
        const int err = libvgmstream_fill(lib, pcm.data(), samples_per_pass);
        if (err < 0) {
          shine_close(enc);
          libvgmstream_free(lib);
          return false;
        }

        int written = 0;
        unsigned char* packet = shine_encode_buffer_interleaved(enc, pcm.data(), &written);
        if (packet == nullptr || written < 0) {
          shine_close(enc);
          libvgmstream_free(lib);
          return false;
        }
        if (written > 0) {
          out.write(reinterpret_cast<const char*>(packet), written);
        }
      }

      int flushed = 0;
      unsigned char* tail = shine_flush(enc, &flushed);
      if (tail != nullptr && flushed > 0) {
        out.write(reinterpret_cast<const char*>(tail), flushed);
      }

      shine_close(enc);
      libvgmstream_free(lib);

      out.flush();
      return out.good() && file_non_empty(target_mp3);
    }

    bool decode_vgmstream_to_mp3(const std::filesystem::path& input,
      const std::filesystem::path& target_mp3) {
      return decode_vgmstream_to_mp3_subsong(input, 0, target_mp3);
    }

    std::optional<int> find_longest_vgmstream_subsong(const std::filesystem::path& input) {
      if (!file_non_empty(input)) {
        return std::nullopt;
      }

      libstreamfile_t* sf = libstreamfile_open_from_stdio(input.string().c_str());
      if (sf == nullptr) {
        return std::nullopt;
      }

      libvgmstream_t* lib = libvgmstream_init();
      if (lib == nullptr) {
        libstreamfile_close(sf);
        return std::nullopt;
      }

      libvgmstream_config_t cfg{};
      cfg.ignore_loop = true;
      cfg.force_sfmt = LIBVGMSTREAM_SFMT_PCM16;
      cfg.auto_downmix_channels = 2;
      libvgmstream_setup(lib, &cfg);

      const int open_default = libvgmstream_open_stream(lib, sf, 0);
      if (open_default < 0 || lib->format == nullptr) {
        libstreamfile_close(sf);
        libvgmstream_free(lib);
        return std::nullopt;
      }

      int subsong_count = lib->format->subsong_count;
      if (subsong_count <= 1) {
        libstreamfile_close(sf);
        libvgmstream_free(lib);
        return std::nullopt;
      }

      int best_subsong = 1;
      int64_t best_samples = 0;
      for (int i = 1; i <= subsong_count; ++i) {
        if (libvgmstream_open_stream(lib, sf, i) < 0 || lib->format == nullptr) {
          continue;
        }

        const int64_t play_samples = lib->format->play_samples;
        if (play_samples > best_samples) {
          best_samples = play_samples;
          best_subsong = i;
        }
      }

      libstreamfile_close(sf);
      libvgmstream_free(lib);
      return best_samples > 0 ? std::optional<int>(best_subsong) : std::nullopt;
    }

    bool write_embedded_png(const std::filesystem::path& source,
      const std::filesystem::path& png_file) {
      std::ifstream in(source, std::ios::binary);
      if (!in) {
        return false;
      }

      in.seekg(0, std::ios::end);
      const auto size = in.tellg();
      if (size <= 0) {
        return false;
      }
      in.seekg(0, std::ios::beg);

      std::vector<uint8_t> bytes(static_cast<std::size_t>(size));
      in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
      if (!in) {
        return false;
      }

      constexpr std::array<uint8_t, 8> kPngSig = {
          0x89U,
          0x50U,
          0x4EU,
          0x47U,
          0x0DU,
          0x0AU,
          0x1AU,
          0x0AU };

      for (std::size_t start = 0; start + kPngSig.size() < bytes.size(); ++start) {
        if (!std::equal(kPngSig.begin(), kPngSig.end(),
          bytes.begin() + static_cast<std::ptrdiff_t>(start))) {
          continue;
        }

        std::size_t chunk = start + kPngSig.size();
        while (chunk + 8 <= bytes.size()) {
          const uint32_t length = read_u32_be(bytes.data() + chunk);
          const std::size_t data_start = chunk + 8;
          const std::size_t data_end = data_start + static_cast<std::size_t>(length);
          const std::size_t crc_end = data_end + 4;
          if (crc_end > bytes.size()) {
            break;
          }

          const std::string type(reinterpret_cast<const char*>(bytes.data() + chunk + 4), 4);
          if (type == "IEND") {
            if (!png_file.parent_path().empty()) {
              std::filesystem::create_directories(png_file.parent_path());
            }
            std::ofstream out(png_file, std::ios::binary | std::ios::trunc);
            if (!out) {
              return false;
            }
            out.write(reinterpret_cast<const char*>(bytes.data() + start),
              static_cast<std::streamsize>(crc_end - start));
            out.flush();
            return out.good() && file_non_empty(png_file);
          }

          chunk = crc_end;
        }
      }

      return false;
    }

    struct UsmKeys {
      std::array<uint8_t, 0x40> video{};
    };

    UsmKeys make_usm_keys(uint64_t key_num) {
      std::array<uint8_t, 8> cipher{};
      for (int i = 0; i < 8; ++i) {
        cipher[static_cast<std::size_t>(i)] =
          static_cast<uint8_t>((key_num >> (8U * static_cast<unsigned>(i))) & 0xFFU);
      }

      std::array<uint8_t, 0x20> key{};
      key[0x00] = cipher[0x00];
      key[0x01] = cipher[0x01];
      key[0x02] = cipher[0x02];
      key[0x03] = static_cast<uint8_t>(cipher[0x03] - 0x34U);
      key[0x04] = static_cast<uint8_t>(cipher[0x04] + 0xF9U);
      key[0x05] = static_cast<uint8_t>(cipher[0x05] ^ 0x13U);
      key[0x06] = static_cast<uint8_t>(cipher[0x06] + 0x61U);
      key[0x07] = static_cast<uint8_t>(key[0x00] ^ 0xFFU);
      key[0x08] = static_cast<uint8_t>(key[0x01] + key[0x02]);
      key[0x09] = static_cast<uint8_t>(key[0x01] - key[0x07]);
      key[0x0A] = static_cast<uint8_t>(key[0x02] ^ 0xFFU);
      key[0x0B] = static_cast<uint8_t>(key[0x01] ^ 0xFFU);
      key[0x0C] = static_cast<uint8_t>(key[0x0B] + key[0x09]);
      key[0x0D] = static_cast<uint8_t>(key[0x08] - key[0x03]);
      key[0x0E] = static_cast<uint8_t>(key[0x0D] ^ 0xFFU);
      key[0x0F] = static_cast<uint8_t>(key[0x0A] - key[0x0B]);
      key[0x10] = static_cast<uint8_t>(key[0x08] - key[0x0F]);
      key[0x11] = static_cast<uint8_t>(key[0x10] ^ key[0x07]);
      key[0x12] = static_cast<uint8_t>(key[0x0F] ^ 0xFFU);
      key[0x13] = static_cast<uint8_t>(key[0x03] ^ 0x10U);
      key[0x14] = static_cast<uint8_t>(key[0x04] - 0x32U);
      key[0x15] = static_cast<uint8_t>(key[0x05] + 0xEDU);
      key[0x16] = static_cast<uint8_t>(key[0x06] ^ 0xF3U);
      key[0x17] = static_cast<uint8_t>(key[0x13] - key[0x0F]);
      key[0x18] = static_cast<uint8_t>(key[0x15] + key[0x07]);
      key[0x19] = static_cast<uint8_t>(0x21U - key[0x13]);
      key[0x1A] = static_cast<uint8_t>(key[0x14] ^ key[0x17]);
      key[0x1B] = static_cast<uint8_t>(key[0x16] + key[0x16]);
      key[0x1C] = static_cast<uint8_t>(key[0x17] + 0x44U);
      key[0x1D] = static_cast<uint8_t>(key[0x03] + key[0x04]);
      key[0x1E] = static_cast<uint8_t>(key[0x05] - key[0x16]);
      key[0x1F] = static_cast<uint8_t>(key[0x1D] ^ key[0x13]);

      UsmKeys out{};
      for (std::size_t i = 0; i < 0x20; ++i) {
        out.video[i] = key[i];
        out.video[0x20 + i] = static_cast<uint8_t>(key[i] ^ 0xFFU);
      }
      return out;
    }

    std::vector<uint8_t> decrypt_usm_video_packet(const std::vector<uint8_t>& packet,
      const std::array<uint8_t, 0x40>& video_key) {
      std::vector<uint8_t> data = packet;
      if (data.size() < 0x40 + 0x200) {
        return data;
      }

      std::array<uint8_t, 0x40> rolling = video_key;
      const std::size_t encrypted_size = data.size() - 0x40;

      for (std::size_t i = 0x100; i < encrypted_size; ++i) {
        const std::size_t packet_index = 0x40 + i;
        const std::size_t key_index = 0x20 + (i % 0x20);
        data[packet_index] = static_cast<uint8_t>(data[packet_index] ^ rolling[key_index]);
        rolling[key_index] = static_cast<uint8_t>(data[packet_index] ^ video_key[key_index]);
      }

      for (std::size_t i = 0; i < 0x100; ++i) {
        const std::size_t key_index = i % 0x20;
        rolling[key_index] = static_cast<uint8_t>(rolling[key_index] ^ data[0x140 + i]);
        data[0x40 + i] = static_cast<uint8_t>(data[0x40 + i] ^ rolling[key_index]);
      }

      return data;
    }

    std::vector<uint8_t> encrypt_usm_video_packet(const std::vector<uint8_t>& packet,
      const std::array<uint8_t, 0x40>& video_key) {
      std::vector<uint8_t> data = packet;
      if (data.size() < 0x40 + 0x200) {
        return data;
      }

      const std::vector<uint8_t> plain = packet;
      std::array<uint8_t, 0x40> rolling = video_key;
      const std::size_t encrypted_size = data.size() - 0x40;

      for (std::size_t i = 0x100; i < encrypted_size; ++i) {
        const std::size_t packet_index = 0x40 + i;
        const std::size_t key_index = 0x20 + (i % 0x20);
        const uint8_t plain_byte = plain[packet_index];
        data[packet_index] = static_cast<uint8_t>(plain_byte ^ rolling[key_index]);
        rolling[key_index] = static_cast<uint8_t>(plain_byte ^ video_key[key_index]);
      }

      for (std::size_t i = 0; i < 0x100; ++i) {
        const std::size_t key_index = i % 0x20;
        rolling[key_index] = static_cast<uint8_t>(rolling[key_index] ^ plain[0x140 + i]);
        data[0x40 + i] = static_cast<uint8_t>(plain[0x40 + i] ^ rolling[key_index]);
      }

      return data;
    }

    enum class VideoCodec {
      kUnknown,
      kVp9Ivf,
    };

    std::size_t find_vp9_ivf_start(const std::vector<uint8_t>& data) {
      constexpr std::array<uint8_t, 4> kIvfSig = {
          static_cast<uint8_t>('D'),
          static_cast<uint8_t>('K'),
          static_cast<uint8_t>('I'),
          static_cast<uint8_t>('F') };
      constexpr std::array<uint8_t, 4> kVp9Sig = {
          static_cast<uint8_t>('V'),
          static_cast<uint8_t>('P'),
          static_cast<uint8_t>('9'),
          static_cast<uint8_t>('0') };

      if (data.size() < 32U) {
        return std::string::npos;
      }

      for (std::size_t i = 0; i + 32U <= data.size(); ++i) {
        if (!std::equal(kIvfSig.begin(), kIvfSig.end(), data.begin() + static_cast<std::ptrdiff_t>(i))) {
          continue;
        }
        if (!std::equal(kVp9Sig.begin(), kVp9Sig.end(), data.begin() + static_cast<std::ptrdiff_t>(i + 8U))) {
          continue;
        }
        const uint16_t header_size = read_u16_le(data.data() + i + 6U);
        if (header_size >= 32U && i + static_cast<std::size_t>(header_size) <= data.size()) {
          return i;
        }
      }

      return std::string::npos;
    }

    bool is_vp9_ivf_stream(const std::vector<uint8_t>& data) {
      return find_vp9_ivf_start(data) != std::string::npos;
    }

    VideoCodec detect_video_codec(const std::vector<uint8_t>& data) {
      if (is_vp9_ivf_stream(data)) {
        return VideoCodec::kVp9Ivf;
      }
      return VideoCodec::kUnknown;
    }
    struct UsmVideoStream {
      std::vector<uint8_t> data;
      VideoCodec codec = VideoCodec::kUnknown;
    };

    bool extract_usm_video_stream(const std::filesystem::path& source, UsmVideoStream& out) {
      std::ifstream in(source, std::ios::binary);
      if (!in) {
        return false;
      }

      constexpr uint64_t kUsmKey = 0x7F4551499DF55E68ULL;
      constexpr std::array<uint8_t, 4> kSfv = {
          static_cast<uint8_t>('@'),
          static_cast<uint8_t>('S'),
          static_cast<uint8_t>('F'),
          static_cast<uint8_t>('V') };

      struct ChannelData {
        bool seen = false;
        VideoCodec codec = VideoCodec::kUnknown;
        std::vector<uint8_t> data;
      };

      std::array<ChannelData, 256> channels{};
      std::vector<uint8_t> channel_order;

      const UsmKeys keys = make_usm_keys(kUsmKey);
      std::array<uint8_t, 0x20> header{};

      while (true) {
        in.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
        const std::streamsize read_bytes = in.gcount();
        if (read_bytes == 0) {
          break;
        }
        if (read_bytes != static_cast<std::streamsize>(header.size())) {
          return false;
        }

        const uint32_t chunk_size_after_header = read_u32_be(header.data() + 4);
        const uint32_t payload_offset = header[9];
        const uint32_t padding_size = read_u16_be(header.data() + 10);
        const uint8_t channel_number = header[12];
        const uint8_t payload_type = static_cast<uint8_t>(header[15] & 0x03U);

        if (chunk_size_after_header < payload_offset + padding_size) {
          return false;
        }

        const uint32_t payload_size = chunk_size_after_header - payload_offset - padding_size;
        const uint32_t extra_offset_bytes = payload_offset > 0x18U ? payload_offset - 0x18U : 0U;
        if (extra_offset_bytes > 0U) {
          in.seekg(static_cast<std::streamoff>(extra_offset_bytes), std::ios::cur);
          if (!in) {
            return false;
          }
        }

        std::vector<uint8_t> payload(payload_size);
        if (!read_exact(in, payload.data(), payload.size())) {
          return false;
        }

        if (padding_size > 0U) {
          in.seekg(static_cast<std::streamoff>(padding_size), std::ios::cur);
          if (!in) {
            return false;
          }
        }

        if (payload_type != 0U || payload.empty()) {
          continue;
        }

        if (!std::equal(kSfv.begin(), kSfv.end(), header.begin())) {
          continue;
        }

        std::vector<uint8_t> decoded = decrypt_usm_video_packet(payload, keys.video);
        ChannelData& channel = channels[channel_number];

        if (!channel.seen) {
          channel.seen = true;
          channel_order.push_back(channel_number);
        }

        channel.data.insert(channel.data.end(), decoded.begin(), decoded.end());
        if (channel.codec == VideoCodec::kUnknown && is_vp9_ivf_stream(channel.data)) {
          channel.codec = VideoCodec::kVp9Ivf;
        }
      }

      if (channel_order.empty()) {
        return false;
      }

      uint8_t selected_channel = channel_order.front();
      for (const uint8_t c : channel_order) {
        if (channels[c].codec == VideoCodec::kVp9Ivf) {
          selected_channel = c;
          break;
        }
      }
      out.data = std::move(channels[selected_channel].data);
      out.codec = detect_video_codec(out.data);
      return !out.data.empty();
    }

    bool fallback_ffmpeg_vp9_to_h264(const std::vector<uint8_t>& vp9_ivf,
      const std::filesystem::path& target_mp4) {
      if (vp9_ivf.empty()) {
        return false;
      }

      const auto tmp_dir = make_temp_work_dir();
      const auto tmp_ivf = tmp_dir / "input.ivf";

      {
        std::ofstream out(tmp_ivf, std::ios::binary | std::ios::trunc);
        if (!out) {
          std::error_code ec;
          std::filesystem::remove_all(tmp_dir, ec);
          return false;
        }
        out.write(reinterpret_cast<const char*>(vp9_ivf.data()), static_cast<std::streamsize>(vp9_ivf.size()));
        out.flush();
      }

      if (!tmp_ivf.parent_path().empty()) {
        std::filesystem::create_directories(tmp_ivf.parent_path());
      }
      if (!target_mp4.parent_path().empty()) {
        std::filesystem::create_directories(target_mp4.parent_path());
      }

#if defined(_WIN32)
      const std::wstring cmd =
        L"ffmpeg -y -loglevel error -i \"" +
        tmp_ivf.wstring() +
        L"\" -an -c:v libx264 -pix_fmt yuv420p \"" +
        target_mp4.wstring() +
        L"\"";
      const int rc = _wsystem(cmd.c_str());
#else
      const std::string cmd =
        "ffmpeg -y -loglevel error -i \"" +
        tmp_ivf.string() +
        "\" -an -c:v libx264 -pix_fmt yuv420p \"" +
        target_mp4.string() +
        "\"";
      const int rc = std::system(cmd.c_str());
#endif

      std::error_code ec;
      std::filesystem::remove_all(tmp_dir, ec);
      return rc == 0 && file_non_empty(target_mp4);
    }

    bool transcode_mp4_to_vp9_ivf(const std::filesystem::path& source_mp4,
      const std::filesystem::path& target_ivf) {
      if (!file_non_empty(source_mp4)) {
        return false;
      }
      if (!target_ivf.parent_path().empty()) {
        std::filesystem::create_directories(target_ivf.parent_path());
      }

#if defined(_WIN32)
      const std::wstring cmd =
        L"ffmpeg -y -loglevel error -i \"" + source_mp4.wstring() +
        L"\" -an -c:v libvpx-vp9 -f ivf \"" + target_ivf.wstring() + L"\"";
      const int rc = _wsystem(cmd.c_str());
#else
      const std::string cmd =
        "ffmpeg -y -loglevel error -i \"" + source_mp4.string() +
        "\" -an -c:v libvpx-vp9 -f ivf \"" + target_ivf.string() + "\"";
      const int rc = std::system(cmd.c_str());
#endif
      return rc == 0 && file_non_empty(target_ivf);
    }

    bool read_binary_file(const std::filesystem::path& path, std::vector<uint8_t>& out) {
      std::ifstream in(path, std::ios::binary);
      if (!in) {
        return false;
      }
      in.seekg(0, std::ios::end);
      const auto size = in.tellg();
      if (size <= 0) {
        return false;
      }
      in.seekg(0, std::ios::beg);
      out.resize(static_cast<std::size_t>(size));
      in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
      return static_cast<std::size_t>(in.gcount()) == out.size();
    }

    bool write_binary_file(const std::filesystem::path& path, const std::vector<uint8_t>& data) {
      if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path());
      }
      std::ofstream out(path, std::ios::binary | std::ios::trunc);
      if (!out) {
        return false;
      }
      out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
      out.flush();
      return out.good() && file_non_empty(path);
    }

    struct UsmVideoPayloadChunk {
      std::size_t payload_begin = 0;
      std::size_t payload_size = 0;
      std::vector<uint8_t> decoded_payload;
    };

    bool patch_template_dat_video_payloads(const std::filesystem::path& template_dat,
      const std::vector<uint8_t>& new_vp9_ivf,
      const std::filesystem::path& target_dat) {
      std::vector<uint8_t> bytes;
      if (!read_binary_file(template_dat, bytes) || bytes.size() < 0x20) {
        return false;
      }

      constexpr std::array<uint8_t, 4> kSfv = {
        static_cast<uint8_t>('@'), static_cast<uint8_t>('S'), static_cast<uint8_t>('F'), static_cast<uint8_t>('V') };
      constexpr uint64_t kUsmKey = 0x7F4551499DF55E68ULL;
      const UsmKeys keys = make_usm_keys(kUsmKey);

      struct ChannelPack {
        std::vector<UsmVideoPayloadChunk> chunks;
        std::vector<uint8_t> concatenated;
      };
      std::array<ChannelPack, 256> channels{};

      std::size_t pos = 0;
      while (pos + 0x20 <= bytes.size()) {
        const uint8_t* header = bytes.data() + static_cast<std::ptrdiff_t>(pos);
        const uint32_t chunk_size_after_header = read_u32_be(header + 4);
        const uint32_t payload_offset = header[9];
        const uint32_t padding_size = read_u16_be(header + 10);
        const uint8_t channel_number = header[12];
        const uint8_t payload_type = static_cast<uint8_t>(header[15] & 0x03U);

        const std::size_t chunk_total_size = 0x20U + static_cast<std::size_t>(chunk_size_after_header);
        if (chunk_total_size == 0 || pos + chunk_total_size > bytes.size()) {
          return false;
        }
        if (chunk_size_after_header < payload_offset + padding_size) {
          return false;
        }

        const std::size_t payload_size =
          static_cast<std::size_t>(chunk_size_after_header - payload_offset - padding_size);
        const std::size_t extra_offset = payload_offset > 0x18U
          ? static_cast<std::size_t>(payload_offset - 0x18U)
          : 0U;
        const std::size_t payload_begin = pos + 0x20U + extra_offset;
        if (payload_begin + payload_size > bytes.size()) {
          return false;
        }

        if (payload_type == 0U && payload_size > 0 &&
          std::equal(kSfv.begin(), kSfv.end(), header)) {
          std::vector<uint8_t> payload(payload_size);
          std::copy(bytes.begin() + static_cast<std::ptrdiff_t>(payload_begin),
            bytes.begin() + static_cast<std::ptrdiff_t>(payload_begin + payload_size),
            payload.begin());
          std::vector<uint8_t> decoded = decrypt_usm_video_packet(payload, keys.video);

          UsmVideoPayloadChunk chunk;
          chunk.payload_begin = payload_begin;
          chunk.payload_size = payload_size;
          chunk.decoded_payload = std::move(decoded);

          auto& channel = channels[channel_number];
          channel.concatenated.insert(channel.concatenated.end(),
            chunk.decoded_payload.begin(), chunk.decoded_payload.end());
          channel.chunks.push_back(std::move(chunk));
        }

        pos += chunk_total_size;
      }

      int selected_channel = -1;
      for (int c = 0; c < 256; ++c) {
        if (!channels[static_cast<std::size_t>(c)].chunks.empty() &&
          is_vp9_ivf_stream(channels[static_cast<std::size_t>(c)].concatenated)) {
          selected_channel = c;
          break;
        }
      }
      if (selected_channel < 0) {
        return false;
      }

      auto& target_channel = channels[static_cast<std::size_t>(selected_channel)];
      std::size_t capacity = 0;
      for (const auto& chunk : target_channel.chunks) {
        capacity += chunk.payload_size;
      }
      if (new_vp9_ivf.size() > capacity) {
        return false;
      }

      std::size_t cursor = 0;
      for (auto& chunk : target_channel.chunks) {
        std::vector<uint8_t> plain(chunk.payload_size, 0U);
        const std::size_t n = std::min(chunk.payload_size, new_vp9_ivf.size() - cursor);
        if (n > 0) {
          std::copy(new_vp9_ivf.begin() + static_cast<std::ptrdiff_t>(cursor),
            new_vp9_ivf.begin() + static_cast<std::ptrdiff_t>(cursor + n),
            plain.begin());
          cursor += n;
        }
        std::vector<uint8_t> encrypted = encrypt_usm_video_packet(plain, keys.video);
        if (encrypted.size() != chunk.payload_size) {
          return false;
        }
        std::copy(encrypted.begin(), encrypted.end(),
          bytes.begin() + static_cast<std::ptrdiff_t>(chunk.payload_begin));
      }

      if (cursor != new_vp9_ivf.size()) {
        return false;
      }

      return write_binary_file(target_dat, bytes);
    }

    bool build_minimal_dat_from_vp9_ivf(const std::vector<uint8_t>& vp9_ivf,
      const std::filesystem::path& target_dat) {
      if (!is_vp9_ivf_stream(vp9_ivf)) {
        return false;
      }

      constexpr uint64_t kUsmKey = 0x7F4551499DF55E68ULL;
      const UsmKeys keys = make_usm_keys(kUsmKey);
      constexpr std::size_t kPayloadChunkSize = 0x8000U;

      std::vector<uint8_t> out;
      out.reserve(vp9_ivf.size() + (vp9_ivf.size() / kPayloadChunkSize + 1U) * 0x20U);

      std::size_t cursor = 0;
      while (cursor < vp9_ivf.size()) {
        const std::size_t payload_size = std::min(kPayloadChunkSize, vp9_ivf.size() - cursor);
        std::vector<uint8_t> plain(payload_size);
        std::copy(vp9_ivf.begin() + static_cast<std::ptrdiff_t>(cursor),
          vp9_ivf.begin() + static_cast<std::ptrdiff_t>(cursor + payload_size),
          plain.begin());
        cursor += payload_size;

        const std::vector<uint8_t> encrypted = encrypt_usm_video_packet(plain, keys.video);
        if (encrypted.size() != payload_size) {
          return false;
        }

        const uint16_t padding = static_cast<uint16_t>((0x20U - (payload_size % 0x20U)) % 0x20U);
        const uint32_t chunk_size_after_header = static_cast<uint32_t>(payload_size + padding);

        std::array<uint8_t, 0x20> header{};
        header[0] = static_cast<uint8_t>('@');
        header[1] = static_cast<uint8_t>('S');
        header[2] = static_cast<uint8_t>('F');
        header[3] = static_cast<uint8_t>('V');
        header[4] = static_cast<uint8_t>((chunk_size_after_header >> 24U) & 0xFFU);
        header[5] = static_cast<uint8_t>((chunk_size_after_header >> 16U) & 0xFFU);
        header[6] = static_cast<uint8_t>((chunk_size_after_header >> 8U) & 0xFFU);
        header[7] = static_cast<uint8_t>(chunk_size_after_header & 0xFFU);
        header[9] = 0U;  // payload starts immediately after 0x20-byte chunk header
        header[10] = static_cast<uint8_t>((padding >> 8U) & 0xFFU);
        header[11] = static_cast<uint8_t>(padding & 0xFFU);
        header[12] = 0U; // video channel 0
        header[15] = 0U; // payload type: stream

        out.insert(out.end(), header.begin(), header.end());
        out.insert(out.end(), encrypted.begin(), encrypted.end());
        out.insert(out.end(), padding, 0U);
      }

      return write_binary_file(target_dat, out);
    }

    bool transcode_vp9_ivf_to_h264_mp4(const std::vector<uint8_t>& vp9_ivf,
      const std::filesystem::path& target_mp4) {
      return fallback_ffmpeg_vp9_to_h264(vp9_ivf, target_mp4);
    }

    bool convert_usm_to_mp4(const std::filesystem::path& source,
      const std::filesystem::path& target_mp4) {
      UsmVideoStream stream;
      if (!extract_usm_video_stream(source, stream)) {
        return false;
      }

      if (stream.codec != VideoCodec::kVp9Ivf) {
        return false;
      }

      return transcode_vp9_ivf_to_h264_mp4(stream.data, target_mp4);
    }
  }  // namespace

  bool convert_audio_to_mp3(const std::filesystem::path& source,
    const std::filesystem::path& target_mp3) {
    if (!file_non_empty(source)) {
      return false;
    }

    const std::string ext = lower(source.extension().string());
    if (ext == ".mp3") {
      if (!target_mp3.parent_path().empty()) {
        std::filesystem::create_directories(target_mp3.parent_path());
      }
      std::filesystem::copy_file(source, target_mp3, std::filesystem::copy_options::overwrite_existing);
      return file_non_empty(target_mp3);
    }

    return decode_vgmstream_to_mp3(source, target_mp3);
  }

  bool convert_acb_awb_to_mp3(const std::filesystem::path& acb,
    const std::filesystem::path& awb,
    const std::filesystem::path& target_mp3) {
    if (!file_non_empty(acb) || !file_non_empty(awb)) {
      return false;
    }

    const auto tmp_dir = make_temp_work_dir();
    std::filesystem::path decode_acb = acb;

    const bool same_parent = acb.parent_path() == awb.parent_path();
    const bool same_stem = lower(acb.stem().string()) == lower(awb.stem().string());
    if (!same_parent || !same_stem) {
      decode_acb = tmp_dir / acb.filename();
      const auto staged_awb = tmp_dir / awb.filename();

      std::error_code copy_ec;
      std::filesystem::copy_file(acb, decode_acb, std::filesystem::copy_options::overwrite_existing, copy_ec);
      if (copy_ec) {
        std::filesystem::remove_all(tmp_dir, copy_ec);
        return false;
      }

      std::filesystem::copy_file(awb, staged_awb, std::filesystem::copy_options::overwrite_existing, copy_ec);
      if (copy_ec) {
        std::filesystem::remove_all(tmp_dir, copy_ec);
        return false;
      }

      const auto expected_awb = decode_acb.parent_path() / (decode_acb.stem().string() + ".awb");
      if (lower(expected_awb.filename().string()) != lower(staged_awb.filename().string())) {
        std::filesystem::copy_file(staged_awb, expected_awb, std::filesystem::copy_options::overwrite_existing, copy_ec);
        if (copy_ec) {
          std::filesystem::remove_all(tmp_dir, copy_ec);
          return false;
        }
      }
    }

    // Support maiconv's lightweight ACB stub for mp3->acb+awb roundtrip.
    std::string awb_name_from_stub;
    uint64_t awb_size_from_stub = 0;
    if (read_acb_stub_sidecar_awb_name(acb, awb_name_from_stub, awb_size_from_stub)) {
      std::error_code ec;
      const auto actual_awb_size = std::filesystem::file_size(awb, ec);
      const bool awb_name_matches = awb_name_from_stub.empty() ||
        lower(awb_name_from_stub) == lower(awb.filename().string());
      if (!ec && awb_name_matches && actual_awb_size == awb_size_from_stub && is_mp3_like_file(awb)) {
        if (!target_mp3.parent_path().empty()) {
          std::filesystem::create_directories(target_mp3.parent_path());
        }
        std::filesystem::copy_file(awb, target_mp3, std::filesystem::copy_options::overwrite_existing, ec);
        std::filesystem::remove_all(tmp_dir, ec);
        return !ec && file_non_empty(target_mp3);
      }
    }

    // Some charts use ACB mainly as metadata while payload is in AWB.
    // Fall back to decoding AWB directly when ACB open/decode fails.
    bool ok = decode_vgmstream_to_mp3(decode_acb, target_mp3);
    if (!ok) {
      std::filesystem::path decode_awb = awb;
      if (!same_parent || !same_stem) {
        decode_awb = tmp_dir / awb.filename();
      }
      ok = decode_vgmstream_to_mp3(decode_awb, target_mp3);

      // XV2-style idea: containers may have multiple cues/entries;
      // if default stream fails, pick the longest subsong as a robust fallback.
      if (!ok) {
        const auto longest_subsong = find_longest_vgmstream_subsong(decode_awb);
        if (longest_subsong.has_value()) {
          ok = decode_vgmstream_to_mp3_subsong(decode_awb, *longest_subsong, target_mp3);
        }
      }
    }

    std::error_code ec;
    std::filesystem::remove_all(tmp_dir, ec);
    return ok;
  }

  bool convert_mp3_to_acb_awb(const std::filesystem::path& source_mp3,
    const std::filesystem::path& target_acb,
    const std::filesystem::path& target_awb) {
    if (!file_non_empty(source_mp3)) {
      return false;
    }

    if (lower(source_mp3.extension().string()) != ".mp3") {
      return false;
    }

    if (!target_awb.parent_path().empty()) {
      std::filesystem::create_directories(target_awb.parent_path());
    }
    if (!target_acb.parent_path().empty()) {
      std::filesystem::create_directories(target_acb.parent_path());
    }

    std::error_code ec;
    std::filesystem::copy_file(source_mp3, target_awb, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec || !file_non_empty(target_awb)) {
      return false;
    }

    const auto awb_name = target_awb.filename().string();
    const auto awb_size = std::filesystem::file_size(target_awb, ec);
    if (ec) {
      return false;
    }

    std::ofstream out(target_acb, std::ios::binary | std::ios::trunc);
    if (!out) {
      return false;
    }
    out.write(reinterpret_cast<const char*>(kAcbStubMagic.data()),
      static_cast<std::streamsize>(kAcbStubMagic.size()));
    write_u32_le(out, 1U);
    write_u64_le(out, static_cast<uint64_t>(awb_size));
    write_u32_le(out, static_cast<uint32_t>(awb_name.size()));
    out.write(awb_name.data(), static_cast<std::streamsize>(awb_name.size()));
    out.flush();

    return out.good() && file_non_empty(target_acb);
  }

  bool convert_ab_to_png(const std::filesystem::path& ab_file,
    const std::filesystem::path& png_file) {
    if (!file_non_empty(ab_file)) {
      return false;
    }
    if (write_embedded_png(ab_file, png_file)) {
      return true;
    }
    return extract_unity_texture_bundle_to_png(ab_file, png_file);
  }

  bool convert_dat_or_usm_to_mp4(const std::filesystem::path& source,
    const std::filesystem::path& target_mp4) {
    if (!file_non_empty(source)) {
      return false;
    }

    return convert_usm_to_mp4(source, target_mp4);
  }

  bool convert_mp4_to_dat(const std::filesystem::path& source_mp4,
    const std::filesystem::path& target_dat) {
    if (!file_non_empty(source_mp4)) {
      return false;
    }

    const auto tmp_dir = make_temp_work_dir();
    const auto vp9_ivf = tmp_dir / "input.ivf";
    std::vector<uint8_t> ivf_bytes;
    const bool converted = transcode_mp4_to_vp9_ivf(source_mp4, vp9_ivf) &&
      read_binary_file(vp9_ivf, ivf_bytes) &&
      build_minimal_dat_from_vp9_ivf(ivf_bytes, target_dat);

    std::error_code ec;
    std::filesystem::remove_all(tmp_dir, ec);
    return converted;
  }

  bool convert_mp4_to_dat_with_template(const std::filesystem::path& source_mp4,
    const std::filesystem::path& template_dat,
    const std::filesystem::path& target_dat) {
    if (!file_non_empty(source_mp4) || !file_non_empty(template_dat)) {
      return false;
    }

    const auto tmp_dir = make_temp_work_dir();
    const auto vp9_ivf = tmp_dir / "input.ivf";
    std::vector<uint8_t> ivf_bytes;
    const bool ready = transcode_mp4_to_vp9_ivf(source_mp4, vp9_ivf) && read_binary_file(vp9_ivf, ivf_bytes);
    const bool converted = ready && patch_template_dat_video_payloads(template_dat, ivf_bytes, target_dat);

    std::error_code ec;
    std::filesystem::remove_all(tmp_dir, ec);
    return converted;
  }

}  // namespace maiconv



















