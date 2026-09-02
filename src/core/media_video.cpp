#include "maiconv/core/media/media_video.hpp"

#include "maiconv/core/io.hpp"

#include "maiconv/core/media/media_shared.hpp"

namespace maiconv {
bool extract_unity_texture_bundle_to_png(const std::filesystem::path &ab_file,
                                         const std::filesystem::path &png_file);
}

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <limits>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#else
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif

namespace maiconv {
namespace {

struct UsmKeys {
  std::array<uint8_t, 0x40> video{};
};

UsmKeys make_usm_keys(uint64_t key_num) {
  std::array<uint8_t, 8> cipher{};
  for (int i = 0; i < 8; ++i) {
    cipher[static_cast<std::size_t>(i)] = static_cast<uint8_t>(
        (key_num >> (8U * static_cast<unsigned>(i))) & 0xFFU);
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

std::vector<uint8_t>
decrypt_usm_video_packet(const std::vector<uint8_t> &packet,
                         const std::array<uint8_t, 0x40> &video_key) {
  std::vector<uint8_t> data = packet;
  if (data.size() < 0x40 + 0x200) {
    return data;
  }

  std::array<uint8_t, 0x40> rolling = video_key;
  const std::size_t encrypted_size = data.size() - 0x40;

  for (std::size_t i = 0x100; i < encrypted_size; ++i) {
    const std::size_t packet_index = 0x40 + i;
    const std::size_t key_index = 0x20 + (i % 0x20);
    data[packet_index] =
        static_cast<uint8_t>(data[packet_index] ^ rolling[key_index]);
    rolling[key_index] =
        static_cast<uint8_t>(data[packet_index] ^ video_key[key_index]);
  }

  for (std::size_t i = 0; i < 0x100; ++i) {
    const std::size_t key_index = i % 0x20;
    rolling[key_index] =
        static_cast<uint8_t>(rolling[key_index] ^ data[0x140 + i]);
    data[0x40 + i] = static_cast<uint8_t>(data[0x40 + i] ^ rolling[key_index]);
  }

  return data;
}

std::vector<uint8_t>
encrypt_usm_video_packet(const std::vector<uint8_t> &packet,
                         const std::array<uint8_t, 0x40> &video_key) {
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
    rolling[key_index] =
        static_cast<uint8_t>(plain_byte ^ video_key[key_index]);
  }

  for (std::size_t i = 0; i < 0x100; ++i) {
    const std::size_t key_index = i % 0x20;
    rolling[key_index] =
        static_cast<uint8_t>(rolling[key_index] ^ plain[0x140 + i]);
    data[0x40 + i] = static_cast<uint8_t>(plain[0x40 + i] ^ rolling[key_index]);
  }

  return data;
}

enum class VideoCodec {
  kUnknown,
  kVp9Ivf,
  kH264AnnexB,
  kMpegVideo,
};

std::size_t find_vp9_ivf_start(const std::vector<uint8_t> &data) {
  constexpr std::array<uint8_t, 4> kIvfSig = {
      static_cast<uint8_t>('D'), static_cast<uint8_t>('K'),
      static_cast<uint8_t>('I'), static_cast<uint8_t>('F')};
  constexpr std::array<uint8_t, 4> kVp9Sig = {
      static_cast<uint8_t>('V'), static_cast<uint8_t>('P'),
      static_cast<uint8_t>('9'), static_cast<uint8_t>('0')};

  if (data.size() < 32U) {
    return std::string::npos;
  }

  for (std::size_t i = 0; i + 32U <= data.size(); ++i) {
    if (!std::equal(kIvfSig.begin(), kIvfSig.end(),
                    data.begin() + static_cast<std::ptrdiff_t>(i))) {
      continue;
    }
    if (!std::equal(kVp9Sig.begin(), kVp9Sig.end(),
                    data.begin() + static_cast<std::ptrdiff_t>(i + 8U))) {
      continue;
    }
    const uint16_t header_size = media_shared_read_u16_le(data.data() + i + 6U);
    if (header_size >= 32U &&
        i + static_cast<std::size_t>(header_size) <= data.size()) {
      return i;
    }
  }

  return std::string::npos;
}

bool is_vp9_ivf_stream(const std::vector<uint8_t> &data) {
  return find_vp9_ivf_start(data) != std::string::npos;
}

bool is_h264_annexb_stream(const std::vector<uint8_t> &data) {
  if (data.size() < 5U) {
    return false;
  }

  auto is_h264_nal = [](uint8_t nal_header) {
    const uint8_t nal_type = static_cast<uint8_t>(nal_header & 0x1FU);
    return nal_type == 1U || nal_type == 5U || nal_type == 6U ||
           nal_type == 7U || nal_type == 8U || nal_type == 9U;
  };

  for (std::size_t i = 0; i + 4U < data.size(); ++i) {
    if (data[i] != 0U || data[i + 1U] != 0U) {
      continue;
    }

    if (data[i + 2U] == 1U) {
      if (is_h264_nal(data[i + 3U])) {
        return true;
      }
      continue;
    }

    if (data[i + 2U] == 0U && data[i + 3U] == 1U && i + 4U < data.size()) {
      if (is_h264_nal(data[i + 4U])) {
        return true;
      }
    }
  }
  return false;
}

bool is_mpeg_video_stream(const std::vector<uint8_t> &data) {
  if (data.size() < 4U) {
    return false;
  }
  for (std::size_t i = 0; i + 4U <= data.size(); ++i) {
    if (data[i] != 0U || data[i + 1U] != 0U || data[i + 2U] != 1U) {
      continue;
    }
    const uint8_t code = data[i + 3U];
    if (code == 0x00U || code == 0xB3U || code == 0xB8U) {
      return true;
    }
  }
  return false;
}

VideoCodec detect_video_codec(const std::vector<uint8_t> &data) {
  if (is_vp9_ivf_stream(data)) {
    return VideoCodec::kVp9Ivf;
  }
  if (is_h264_annexb_stream(data)) {
    return VideoCodec::kH264AnnexB;
  }
  if (is_mpeg_video_stream(data)) {
    return VideoCodec::kMpegVideo;
  }
  return VideoCodec::kUnknown;
}
struct UsmVideoStream {
  std::vector<uint8_t> data;
  VideoCodec codec = VideoCodec::kUnknown;
};

bool extract_usm_video_stream(const std::filesystem::path &source,
                              UsmVideoStream &out) {
  std::ifstream in(source, std::ios::binary);
  if (!in) {
    return false;
  }

  constexpr uint64_t kUsmKey = 0x7F4551499DF55E68ULL;
  constexpr std::array<uint8_t, 4> kSfv = {
      static_cast<uint8_t>('@'), static_cast<uint8_t>('S'),
      static_cast<uint8_t>('F'), static_cast<uint8_t>('V')};

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
    in.read(reinterpret_cast<char *>(header.data()),
            static_cast<std::streamsize>(header.size()));
    const std::streamsize read_bytes = in.gcount();
    if (read_bytes == 0) {
      break;
    }
    if (read_bytes != static_cast<std::streamsize>(header.size())) {
      return false;
    }

    const uint32_t chunk_size_after_header =
        media_shared_read_u32_be(header.data() + 4);
    const uint32_t payload_offset = header[9];
    const uint32_t padding_size = media_shared_read_u16_be(header.data() + 10);
    const uint8_t channel_number = header[12];
    const uint8_t payload_type = static_cast<uint8_t>(header[15] & 0x03U);

    if (chunk_size_after_header < payload_offset + padding_size) {
      return false;
    }

    const uint32_t payload_size =
        chunk_size_after_header - payload_offset - padding_size;
    const uint32_t extra_offset_bytes =
        payload_offset > 0x18U ? payload_offset - 0x18U : 0U;
    if (extra_offset_bytes > 0U) {
      in.seekg(static_cast<std::streamoff>(extra_offset_bytes), std::ios::cur);
      if (!in) {
        return false;
      }
    }

    std::vector<uint8_t> payload(payload_size);
    if (!media_shared_read_exact(in, payload.data(), payload.size())) {
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

    std::vector<uint8_t> decoded =
        decrypt_usm_video_packet(payload, keys.video);
    ChannelData &channel = channels[channel_number];

    if (!channel.seen) {
      channel.seen = true;
      channel_order.push_back(channel_number);
    }

    channel.data.insert(channel.data.end(), decoded.begin(), decoded.end());
    if (channel.codec == VideoCodec::kUnknown) {
      channel.codec = detect_video_codec(channel.data);
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

bool fallback_ffmpeg_vp9_to_h264(const std::vector<uint8_t> &vp9_ivf,
                                 const std::filesystem::path &target_mp4) {
  if (vp9_ivf.empty()) {
    return false;
  }

  if (!target_mp4.parent_path().empty()) {
    std::filesystem::create_directories(target_mp4.parent_path());
  }

  const auto h264_encoders = media_shared_resolve_ffmpeg_h264_encoders();
  if (h264_encoders.empty()) {
    return false;
  }

  for (const auto &encoder : h264_encoders) {
    media_shared_remove_file_if_exists(target_mp4);
#if defined(_WIN32)
    std::vector<std::wstring> args = {L"-y", L"-loglevel", L"error"};
    media_shared_append_hwaccel_arg(args);
    args.insert(args.end(), {L"-f", L"ivf", L"-i", L"pipe:0", L"-an", L"-c:v",
                             media_shared_widen_ascii(encoder), L"-pix_fmt",
                             L"yuv420p", target_mp4.wstring()});
    const bool ok = media_shared_run_ffmpeg_feed_stdin(args, vp9_ivf);
#else
    std::vector<std::string> args = {"-y", "-loglevel", "error"};
    media_shared_append_hwaccel_arg(args);
    args.insert(args.end(),
                {"-f", "ivf", "-i", "pipe:0", "-an", "-c:v", encoder,
                 "-pix_fmt", "yuv420p", media_shared_path_to_utf8(target_mp4)});
    const bool ok = media_shared_run_ffmpeg_feed_stdin(args, vp9_ivf);
#endif
    if (ok && media_shared_file_non_empty(target_mp4)) {
      return true;
    }
  }

  return false;
}

bool transcode_mp4_to_vp9_ivf_bytes(const std::filesystem::path &source_mp4,
                                    std::vector<uint8_t> &target_ivf) {
  if (!media_shared_file_non_empty(source_mp4)) {
    return false;
  }

  target_ivf.clear();

  const auto vp9_encoders = media_shared_resolve_ffmpeg_vp9_encoders();
  if (vp9_encoders.empty()) {
    return false;
  }

  for (const auto &encoder : vp9_encoders) {
    target_ivf.clear();
#if defined(_WIN32)
    std::vector<std::wstring> args = {L"-y", L"-loglevel", L"error"};
    media_shared_append_hwaccel_arg(args);
    args.insert(args.end(),
                {L"-i", source_mp4.wstring(), L"-an", L"-c:v",
                 media_shared_widen_ascii(encoder), L"-f", L"ivf", L"pipe:1"});
    const bool ok = media_shared_run_ffmpeg_capture_stdout(args, target_ivf);
#else
    std::vector<std::string> args = {"-y", "-loglevel", "error"};
    media_shared_append_hwaccel_arg(args);
    args.insert(args.end(), {"-i", media_shared_path_to_utf8(source_mp4), "-an",
                             "-c:v", encoder, "-f", "ivf", "pipe:1"});
    const bool ok = media_shared_run_ffmpeg_capture_stdout(args, target_ivf);
#endif
    if (ok && is_vp9_ivf_stream(target_ivf)) {
      return true;
    }
  }

  target_ivf.clear();
  return false;
}

bool build_minimal_dat_from_vp9_ivf(const std::vector<uint8_t> &vp9_ivf,
                                    const std::filesystem::path &target_dat) {
  if (!is_vp9_ivf_stream(vp9_ivf)) {
    return false;
  }

  constexpr uint64_t kUsmKey = 0x7F4551499DF55E68ULL;
  const UsmKeys keys = make_usm_keys(kUsmKey);
  constexpr std::size_t kPayloadChunkSize = 0x8000U;

  std::vector<uint8_t> out;
  out.reserve(vp9_ivf.size() +
              (vp9_ivf.size() / kPayloadChunkSize + 1U) * 0x20U);

  std::size_t cursor = 0;
  while (cursor < vp9_ivf.size()) {
    const std::size_t remaining = vp9_ivf.size() - cursor;
    const std::size_t payload_size =
        remaining < kPayloadChunkSize ? remaining : kPayloadChunkSize;
    std::vector<uint8_t> plain(payload_size);
    std::copy(vp9_ivf.begin() + static_cast<std::ptrdiff_t>(cursor),
              vp9_ivf.begin() +
                  static_cast<std::ptrdiff_t>(cursor + payload_size),
              plain.begin());
    cursor += payload_size;

    const std::vector<uint8_t> encrypted =
        encrypt_usm_video_packet(plain, keys.video);
    if (encrypted.size() != payload_size) {
      return false;
    }

    const uint16_t padding =
        static_cast<uint16_t>((0x20U - (payload_size % 0x20U)) % 0x20U);
    const uint32_t chunk_size_after_header =
        static_cast<uint32_t>(payload_size + padding);

    std::array<uint8_t, 0x20> header{};
    header[0] = static_cast<uint8_t>('@');
    header[1] = static_cast<uint8_t>('S');
    header[2] = static_cast<uint8_t>('F');
    header[3] = static_cast<uint8_t>('V');
    header[4] = static_cast<uint8_t>((chunk_size_after_header >> 24U) & 0xFFU);
    header[5] = static_cast<uint8_t>((chunk_size_after_header >> 16U) & 0xFFU);
    header[6] = static_cast<uint8_t>((chunk_size_after_header >> 8U) & 0xFFU);
    header[7] = static_cast<uint8_t>(chunk_size_after_header & 0xFFU);
    header[9] = 0U; // payload starts immediately after 0x20-byte chunk header
    header[10] = static_cast<uint8_t>((padding >> 8U) & 0xFFU);
    header[11] = static_cast<uint8_t>(padding & 0xFFU);
    header[12] = 0U; // video channel 0
    header[15] = 0U; // payload type: stream

    out.insert(out.end(), header.begin(), header.end());
    out.insert(out.end(), encrypted.begin(), encrypted.end());
    out.insert(out.end(), padding, 0U);
  }

  return media_shared_write_binary_file(target_dat, out);
}

bool transcode_vp9_ivf_to_h264_mp4(const std::vector<uint8_t> &vp9_ivf,
                                   const std::filesystem::path &target_mp4) {
  return fallback_ffmpeg_vp9_to_h264(vp9_ivf, target_mp4);
}

std::string usm_stream_extension(VideoCodec codec) {
  switch (codec) {
  case VideoCodec::kVp9Ivf:
    return ".ivf";
  case VideoCodec::kH264AnnexB:
    return ".h264";
  case VideoCodec::kMpegVideo:
    return ".m1v";
  default:
    return ".bin";
  }
}

bool remux_extracted_stream_to_mp4(const std::filesystem::path &stream_file,
                                   const std::filesystem::path &target_mp4) {
  if (!media_shared_file_non_empty(stream_file)) {
    return false;
  }
  if (!target_mp4.parent_path().empty()) {
    std::filesystem::create_directories(target_mp4.parent_path());
  }

#if defined(_WIN32)
  std::vector<std::wstring> args = {L"-y", L"-loglevel", L"error"};
  args.insert(args.end(), {L"-i", stream_file.wstring(), L"-an", L"-c:v",
                           L"copy", target_mp4.wstring()});
  const bool ok = media_shared_run_ffmpeg_process(args);
#else
  std::vector<std::string> args = {"-y", "-loglevel", "error"};
  args.insert(args.end(),
              {"-i", media_shared_path_to_utf8(stream_file), "-an", "-c:v",
               "copy", media_shared_path_to_utf8(target_mp4)});
  const bool ok = media_shared_run_ffmpeg_process(args);
#endif
  return ok && media_shared_file_non_empty(target_mp4);
}

bool transcode_extracted_stream_to_h264_mp4(
    const std::filesystem::path &stream_file,
    const std::filesystem::path &target_mp4) {
  if (!media_shared_file_non_empty(stream_file)) {
    return false;
  }
  if (!target_mp4.parent_path().empty()) {
    std::filesystem::create_directories(target_mp4.parent_path());
  }

  const auto h264_encoders = media_shared_resolve_ffmpeg_h264_encoders();
  if (h264_encoders.empty()) {
    return false;
  }

  for (const auto &encoder : h264_encoders) {
    media_shared_remove_file_if_exists(target_mp4);
#if defined(_WIN32)
    std::vector<std::wstring> args = {L"-y", L"-loglevel", L"error"};
    media_shared_append_hwaccel_arg(args);
    args.insert(args.end(), {L"-i", stream_file.wstring(), L"-an", L"-c:v",
                             media_shared_widen_ascii(encoder), L"-pix_fmt",
                             L"yuv420p", target_mp4.wstring()});
    const bool ok = media_shared_run_ffmpeg_process(args);
#else
    std::vector<std::string> args = {"-y", "-loglevel", "error"};
    media_shared_append_hwaccel_arg(args);
    args.insert(args.end(), {"-i", media_shared_path_to_utf8(stream_file),
                             "-an", "-c:v", encoder, "-pix_fmt", "yuv420p",
                             media_shared_path_to_utf8(target_mp4)});
    const bool ok = media_shared_run_ffmpeg_process(args);
#endif
    if (ok && media_shared_file_non_empty(target_mp4)) {
      return true;
    }
  }

  return false;
}

bool convert_usm_to_mp4(const std::filesystem::path &source,
                        const std::filesystem::path &target_mp4) {
  UsmVideoStream stream;
  if (!extract_usm_video_stream(source, stream)) {
    return false;
  }

  if (stream.codec == VideoCodec::kVp9Ivf) {
    return transcode_vp9_ivf_to_h264_mp4(stream.data, target_mp4);
  }

  const auto tmp_dir = media_shared_make_temp_work_dir();
  const auto stream_file =
      tmp_dir / ("video_stream" + usm_stream_extension(stream.codec));
  if (!media_shared_write_binary_file(stream_file, stream.data)) {
    return false;
  }

  media_shared_remove_file_if_exists(target_mp4);
  if (remux_extracted_stream_to_mp4(stream_file, target_mp4)) {
    return true;
  }

  media_shared_remove_file_if_exists(target_mp4);
  return transcode_extracted_stream_to_h264_mp4(stream_file, target_mp4);
}

} // namespace

bool convert_dat_or_usm_to_mp4(const std::filesystem::path &source,
                               const std::filesystem::path &target_mp4) {
  if (!media_shared_file_non_empty(source)) {
    return false;
  }

  if (convert_usm_to_mp4(source, target_mp4)) {
    return true;
  }

  const auto h264_encoders = media_shared_resolve_ffmpeg_h264_encoders();
  if (h264_encoders.empty()) {
    return false;
  }

  for (const auto &encoder : h264_encoders) {
    media_shared_remove_file_if_exists(target_mp4);
#if defined(_WIN32)
    std::vector<std::wstring> args = {L"-y", L"-loglevel", L"error"};
    media_shared_append_hwaccel_arg(args);
    args.insert(args.end(), {L"-i", source.wstring(), L"-an", L"-c:v",
                             media_shared_widen_ascii(encoder), L"-pix_fmt",
                             L"yuv420p", target_mp4.wstring()});
    const bool fallback_ok = media_shared_run_ffmpeg_process(args);
#else
    std::vector<std::string> args = {"-y", "-loglevel", "error"};
    media_shared_append_hwaccel_arg(args);
    args.insert(args.end(), {"-i", media_shared_path_to_utf8(source), "-an",
                             "-c:v", encoder, "-pix_fmt", "yuv420p",
                             media_shared_path_to_utf8(target_mp4)});
    const bool fallback_ok = media_shared_run_ffmpeg_process(args);
#endif
    if (fallback_ok && media_shared_file_non_empty(target_mp4)) {
      return true;
    }
  }

  return false;
}

bool generate_single_frame_mp4_from_image(
    const std::filesystem::path &source_image,
    const std::filesystem::path &target_mp4) {
  if (!media_shared_file_non_empty(source_image)) {
    return false;
  }
  if (!target_mp4.parent_path().empty()) {
    std::filesystem::create_directories(target_mp4.parent_path());
  }

  const auto h264_encoders = media_shared_resolve_ffmpeg_h264_encoders();
  if (h264_encoders.empty()) {
    return false;
  }

  for (const auto &encoder : h264_encoders) {
    media_shared_remove_file_if_exists(target_mp4);
#if defined(_WIN32)
    std::vector<std::wstring> args = {L"-y", L"-loglevel", L"error"};
    media_shared_append_hwaccel_arg(args);
    args.insert(args.end(),
                {L"-loop", L"1", L"-i", source_image.wstring(), L"-frames:v",
                 L"1", L"-an", L"-c:v", media_shared_widen_ascii(encoder),
                 L"-pix_fmt", L"yuv420p", target_mp4.wstring()});
    const bool ok = media_shared_run_ffmpeg_process(args);
#else
    std::vector<std::string> args = {"-y", "-loglevel", "error"};
    media_shared_append_hwaccel_arg(args);
    args.insert(args.end(),
                {"-loop", "1", "-i", media_shared_path_to_utf8(source_image),
                 "-frames:v", "1", "-an", "-c:v", encoder, "-pix_fmt",
                 "yuv420p", media_shared_path_to_utf8(target_mp4)});
    const bool ok = media_shared_run_ffmpeg_process(args);
#endif
    if (ok && media_shared_file_non_empty(target_mp4)) {
      return true;
    }
  }

  return false;
}

bool generate_single_frame_black_mp4(const std::filesystem::path &target_mp4) {
  if (!target_mp4.parent_path().empty()) {
    std::filesystem::create_directories(target_mp4.parent_path());
  }

  const auto h264_encoders = media_shared_resolve_ffmpeg_h264_encoders();
  if (h264_encoders.empty()) {
    return false;
  }

  for (const auto &encoder : h264_encoders) {
    media_shared_remove_file_if_exists(target_mp4);
#if defined(_WIN32)
    std::vector<std::wstring> args = {L"-y", L"-loglevel", L"error"};
    media_shared_append_hwaccel_arg(args);
    args.insert(args.end(),
                {L"-f", L"lavfi", L"-i", L"color=c=black:s=1280x720:r=1",
                 L"-frames:v", L"1", L"-an", L"-c:v",
                 media_shared_widen_ascii(encoder), L"-pix_fmt", L"yuv420p",
                 target_mp4.wstring()});
    const bool ok = media_shared_run_ffmpeg_process(args);
#else
    std::vector<std::string> args = {"-y", "-loglevel", "error"};
    media_shared_append_hwaccel_arg(args);
    args.insert(args.end(),
                {"-f", "lavfi", "-i", "color=c=black:s=1280x720:r=1",
                 "-frames:v", "1", "-an", "-c:v", encoder, "-pix_fmt",
                 "yuv420p", media_shared_path_to_utf8(target_mp4)});
    const bool ok = media_shared_run_ffmpeg_process(args);
#endif
    if (ok && media_shared_file_non_empty(target_mp4)) {
      return true;
    }
  }

  return false;
}

bool convert_mp4_to_dat(const std::filesystem::path &source_mp4,
                        const std::filesystem::path &target_dat) {
  if (!media_shared_file_non_empty(source_mp4)) {
    return false;
  }

  std::vector<uint8_t> ivf_bytes;
  const bool converted =
      transcode_mp4_to_vp9_ivf_bytes(source_mp4, ivf_bytes) &&
      build_minimal_dat_from_vp9_ivf(ivf_bytes, target_dat);

  return converted;
}

} // namespace maiconv
