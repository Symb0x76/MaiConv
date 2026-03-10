#include "maiconv/core/media.hpp"

#include "maiconv/core/io.hpp"

namespace maiconv {
  bool extract_unity_texture_bundle_to_png(const std::filesystem::path& ab_file,
    const std::filesystem::path& png_file);
}

extern "C" {
#include "layer3.h"
#include "libvgmstream.h"
#include "minimp4.h"
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

    bool copy_if_mp4(const std::filesystem::path& source,
      const std::filesystem::path& target_mp4) {
      std::ifstream in(source, std::ios::binary);
      if (!in) {
        return false;
      }

      std::array<uint8_t, 12> head{};
      in.read(reinterpret_cast<char*>(head.data()), static_cast<std::streamsize>(head.size()));
      if (!in) {
        return false;
      }

      const bool has_ftyp =
        head[4] == static_cast<uint8_t>('f') &&
        head[5] == static_cast<uint8_t>('t') &&
        head[6] == static_cast<uint8_t>('y') &&
        head[7] == static_cast<uint8_t>('p');
      if (!has_ftyp) {
        return false;
      }

      if (!target_mp4.parent_path().empty()) {
        std::filesystem::create_directories(target_mp4.parent_path());
      }
      std::filesystem::copy_file(source, target_mp4, std::filesystem::copy_options::overwrite_existing);
      return file_non_empty(target_mp4);
    }

    bool has_h264_start_code(const std::vector<uint8_t>& data) {
      if (data.size() < 3) {
        return false;
      }

      for (std::size_t i = 0; i + 2 < data.size(); ++i) {
        if (data[i] != 0x00U || data[i + 1] != 0x00U) {
          continue;
        }
        if (data[i + 2] == 0x01U) {
          return true;
        }
        if (i + 3 < data.size() && data[i + 2] == 0x00U && data[i + 3] == 0x01U) {
          return true;
        }
      }

      return false;
    }

    std::size_t find_h264_start_code(const std::vector<uint8_t>& data,
      std::size_t from,
      std::size_t* prefix_length) {
      if (prefix_length == nullptr) {
        return std::string::npos;
      }
      *prefix_length = 0;

      if (data.size() < 3 || from >= data.size()) {
        return std::string::npos;
      }

      for (std::size_t i = from; i + 2 < data.size(); ++i) {
        if (data[i] != 0x00U || data[i + 1] != 0x00U) {
          continue;
        }
        if (data[i + 2] == 0x01U) {
          *prefix_length = 3U;
          return i;
        }
        if (i + 3 < data.size() && data[i + 2] == 0x00U && data[i + 3] == 0x01U) {
          *prefix_length = 4U;
          return i;
        }
      }

      return std::string::npos;
    }

    class H264BitReader {
    public:
      H264BitReader(const uint8_t* data, std::size_t bytes) : data_(data), bytes_(bytes) {}

      bool read_bit(uint32_t& bit) {
        if (bit_offset_ >= bytes_ * 8U) {
          return false;
        }
        const std::size_t byte_index = bit_offset_ / 8U;
        const uint8_t shift = static_cast<uint8_t>(7U - (bit_offset_ % 8U));
        bit = static_cast<uint32_t>((data_[byte_index] >> shift) & 0x01U);
        ++bit_offset_;
        return true;
      }

      bool read_bits(int count, uint32_t& value) {
        if (count < 0 || count > 32) {
          return false;
        }
        value = 0U;
        for (int i = 0; i < count; ++i) {
          uint32_t bit = 0U;
          if (!read_bit(bit)) {
            return false;
          }
          value = static_cast<uint32_t>((value << 1U) | bit);
        }
        return true;
      }

      bool read_ue(uint32_t& value) {
        int leading_zero_bits = 0;
        for (;;) {
          uint32_t bit = 0U;
          if (!read_bit(bit)) {
            return false;
          }
          if (bit != 0U) {
            break;
          }
          ++leading_zero_bits;
          if (leading_zero_bits > 31) {
            return false;
          }
        }

        if (leading_zero_bits == 0) {
          value = 0U;
          return true;
        }

        uint32_t suffix = 0U;
        if (!read_bits(leading_zero_bits, suffix)) {
          return false;
        }

        const uint64_t code_num =
          ((1ULL << static_cast<unsigned>(leading_zero_bits)) - 1ULL) +
          static_cast<uint64_t>(suffix);
        if (code_num > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())) {
          return false;
        }

        value = static_cast<uint32_t>(code_num);
        return true;
      }

    private:
      const uint8_t* data_ = nullptr;
      std::size_t bytes_ = 0;
      std::size_t bit_offset_ = 0;
    };

    bool remove_h264_emulation_bytes(const std::vector<uint8_t>& src,
      std::vector<uint8_t>& out) {
      out.clear();
      out.reserve(src.size());

      for (std::size_t i = 0; i < src.size(); ++i) {
        if (i + 2 < src.size() && src[i] == 0x00U && src[i + 1] == 0x00U && src[i + 2] == 0x03U) {
          out.push_back(0x00U);
          out.push_back(0x00U);
          i += 2U;
          continue;
        }
        out.push_back(src[i]);
      }

      return !out.empty();
    }

    bool extract_first_h264_sps(const std::vector<uint8_t>& annexb,
      std::vector<uint8_t>& sps) {
      std::size_t search_pos = 0;

      while (true) {
        std::size_t prefix_len = 0;
        const std::size_t start_code_pos = find_h264_start_code(annexb, search_pos, &prefix_len);
        if (start_code_pos == std::string::npos) {
          return false;
        }

        const std::size_t nal_start = start_code_pos + prefix_len;
        std::size_t next_prefix_len = 0;
        const std::size_t next_start_code_pos = find_h264_start_code(annexb, nal_start, &next_prefix_len);

        const std::size_t nal_end = next_start_code_pos == std::string::npos ? annexb.size() : next_start_code_pos;

        if (nal_end > nal_start) {
          const uint8_t nal_type = static_cast<uint8_t>(annexb[nal_start] & 0x1FU);
          if (nal_type == 7U) {
            sps.assign(annexb.begin() + static_cast<std::ptrdiff_t>(nal_start),
              annexb.begin() + static_cast<std::ptrdiff_t>(nal_end));
            return true;
          }
        }

        if (next_start_code_pos == std::string::npos) {
          return false;
        }

        search_pos = next_start_code_pos + next_prefix_len;
      }
    }

    bool parse_h264_sps_dimensions(const std::vector<uint8_t>& sps,
      int& width,
      int& height) {
      width = 0;
      height = 0;

      if (sps.size() < 4U) {
        return false;
      }

      std::vector<uint8_t> rbsp;
      if (!remove_h264_emulation_bytes(sps, rbsp) || rbsp.size() < 2U) {
        return false;
      }

      H264BitReader br(rbsp.data() + 1U, rbsp.size() - 1U);

      uint32_t profile_idc = 0U;
      uint32_t ignored = 0U;
      if (!br.read_bits(8, profile_idc) || !br.read_bits(8, ignored) || !br.read_bits(8, ignored)) {
        return false;
      }

      uint32_t seq_parameter_set_id = 0U;
      if (!br.read_ue(seq_parameter_set_id)) {
        return false;
      }

      if (profile_idc == 100U || profile_idc == 110U || profile_idc == 122U || profile_idc == 244U) {
        uint32_t chroma_format_idc = 1U;
        if (!br.read_ue(chroma_format_idc)) {
          return false;
        }
        if (chroma_format_idc == 3U) {
          if (!br.read_bits(1, ignored)) {
            return false;
          }
        }
        if (!br.read_ue(ignored) || !br.read_ue(ignored) || !br.read_bits(1, ignored)) {
          return false;
        }
        uint32_t seq_scaling_matrix_present_flag = 0U;
        if (!br.read_bits(1, seq_scaling_matrix_present_flag)) {
          return false;
        }
        if (seq_scaling_matrix_present_flag != 0U) {
          return false;
        }
      }

      if (!br.read_ue(ignored)) {
        return false;
      }

      uint32_t pic_order_cnt_type = 0U;
      if (!br.read_ue(pic_order_cnt_type)) {
        return false;
      }
      if (pic_order_cnt_type == 0U) {
        if (!br.read_ue(ignored)) {
          return false;
        }
      }
      else if (pic_order_cnt_type == 1U) {
        return false;
      }

      if (!br.read_ue(ignored) || !br.read_bits(1, ignored)) {
        return false;
      }

      uint32_t pic_width_in_mbs_minus1 = 0U;
      uint32_t pic_height_in_map_units_minus1 = 0U;
      if (!br.read_ue(pic_width_in_mbs_minus1) || !br.read_ue(pic_height_in_map_units_minus1)) {
        return false;
      }

      uint32_t frame_mbs_only_flag = 0U;
      if (!br.read_bits(1, frame_mbs_only_flag)) {
        return false;
      }
      if (frame_mbs_only_flag == 0U) {
        if (!br.read_bits(1, ignored)) {
          return false;
        }
      }

      if (!br.read_bits(1, ignored)) {
        return false;
      }

      uint32_t frame_cropping_flag = 0U;
      if (!br.read_bits(1, frame_cropping_flag)) {
        return false;
      }

      uint32_t crop_left = 0U;
      uint32_t crop_right = 0U;
      uint32_t crop_top = 0U;
      uint32_t crop_bottom = 0U;
      if (frame_cropping_flag != 0U) {
        if (!br.read_ue(crop_left) || !br.read_ue(crop_right) ||
          !br.read_ue(crop_top) || !br.read_ue(crop_bottom)) {
          return false;
        }
      }

      const int width_pixels = static_cast<int>((pic_width_in_mbs_minus1 + 1U) * 16U);
      const int height_pixels = static_cast<int>((pic_height_in_map_units_minus1 + 1U) * 16U * (2U - frame_mbs_only_flag));

      width = width_pixels - static_cast<int>((crop_left + crop_right) * 2U);
      height = height_pixels - static_cast<int>((crop_top + crop_bottom) * 2U);

      return width > 0 && height > 0;
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

    enum class VideoCodec {
      kUnknown,
      kH264,
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
      if (has_h264_start_code(data)) {
        return VideoCodec::kH264;
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
        if (channel.codec != VideoCodec::kH264) {
          if (has_h264_start_code(channel.data)) {
            channel.codec = VideoCodec::kH264;
          }
          else if (channel.codec == VideoCodec::kUnknown && is_vp9_ivf_stream(channel.data)) {
            channel.codec = VideoCodec::kVp9Ivf;
          }
        }
      }

      if (channel_order.empty()) {
        return false;
      }

      uint8_t selected_channel = channel_order.front();
      for (const uint8_t c : channel_order) {
        if (channels[c].codec == VideoCodec::kH264) {
          selected_channel = c;
          break;
        }
      }

      if (channels[selected_channel].codec != VideoCodec::kH264) {
        for (const uint8_t c : channel_order) {
          if (channels[c].codec == VideoCodec::kVp9Ivf) {
            selected_channel = c;
            break;
          }
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

    bool transcode_vp9_ivf_to_h264_mp4(const std::vector<uint8_t>& vp9_ivf,
      const std::filesystem::path& target_mp4) {
      return fallback_ffmpeg_vp9_to_h264(vp9_ivf, target_mp4);
    }

#if defined(_WIN32)
    std::FILE* open_file_for_write(const std::filesystem::path& path) {
      std::FILE* file = nullptr;
      if (_wfopen_s(&file, path.c_str(), L"wb") != 0) {
        return nullptr;
      }
      return file;
    }
#else
    std::FILE* open_file_for_write(const std::filesystem::path& path) {
      return std::fopen(path.string().c_str(), "wb");
    }
#endif

    int mp4_write_callback(int64_t offset, const void* buffer, size_t size, void* token) {
      auto* file = static_cast<std::FILE*>(token);
      if (file == nullptr) {
        return -1;
      }
#if defined(_WIN32)
      if (_fseeki64(file, offset, SEEK_SET) != 0) {
        return -1;
      }
#else
      if (fseeko(file, static_cast<off_t>(offset), SEEK_SET) != 0) {
        return -1;
      }
#endif
      return std::fwrite(buffer, 1, size, file) == size ? 0 : -1;
    }

    bool mux_h264_annexb_to_mp4(const std::vector<uint8_t>& h264_annexb,
      const std::filesystem::path& target_mp4) {
      if (!has_h264_start_code(h264_annexb)) {
        return false;
      }

      std::vector<uint8_t> sps;
      if (!extract_first_h264_sps(h264_annexb, sps)) {
        return false;
      }

      int width = 0;
      int height = 0;
      if (!parse_h264_sps_dimensions(sps, width, height)) {
        return false;
      }

      if (!target_mp4.parent_path().empty()) {
        std::filesystem::create_directories(target_mp4.parent_path());
      }

      std::FILE* file = open_file_for_write(target_mp4);
      if (file == nullptr) {
        return false;
      }

      MP4E_mux_t* mux = MP4E_open(0, 0, file, mp4_write_callback);
      if (mux == nullptr) {
        std::fclose(file);
        return false;
      }

      bool ok = true;
      mp4_h26x_writer_t writer{};
      if (mp4_h26x_write_init(&writer, mux, width, height, 0) != MP4E_STATUS_OK) {
        ok = false;
      }

      if (ok) {
        constexpr unsigned kFrameDuration90k = 3000U;
        const int status = mp4_h26x_write_nal(
          &writer,
          h264_annexb.data(),
          static_cast<int>(h264_annexb.size()),
          kFrameDuration90k);
        if (status != MP4E_STATUS_OK) {
          ok = false;
        }
      }

      mp4_h26x_write_close(&writer);

      if (MP4E_close(mux) != MP4E_STATUS_OK) {
        ok = false;
      }
      std::fclose(file);

      if (!ok || !file_non_empty(target_mp4)) {
        std::error_code ec;
        std::filesystem::remove(target_mp4, ec);
        return false;
      }

      return true;
    }

    bool convert_usm_to_mp4(const std::filesystem::path& source,
      const std::filesystem::path& target_mp4) {
      UsmVideoStream stream;
      if (!extract_usm_video_stream(source, stream)) {
        return false;
      }

      if (stream.codec != VideoCodec::kH264) {
        return transcode_vp9_ivf_to_h264_mp4(stream.data, target_mp4);
      }

      return mux_h264_annexb_to_mp4(stream.data, target_mp4);
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

    if (lower(source.extension().string()) == ".mp4") {
      if (!target_mp4.parent_path().empty()) {
        std::filesystem::create_directories(target_mp4.parent_path());
      }
      std::filesystem::copy_file(source, target_mp4, std::filesystem::copy_options::overwrite_existing);
      return file_non_empty(target_mp4);
    }

    if (copy_if_mp4(source, target_mp4)) {
      return true;
    }

    return convert_usm_to_mp4(source, target_mp4);
  }

}  // namespace maiconv



















