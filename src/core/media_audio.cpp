#include "maiconv/core/media/media_audio.hpp"

#include "maiconv/core/media/media_shared.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <stdexcept>

namespace maiconv {

bool convert_audio_to_mp3(const std::filesystem::path &source,
                          const std::filesystem::path &target_mp3) {
  if (!media_shared_file_non_empty(source)) {
    return false;
  }

  const std::string ext = media_shared_lower(source.extension().string());
  if (ext == ".mp3") {
    if (!target_mp3.parent_path().empty()) {
      std::filesystem::create_directories(target_mp3.parent_path());
    }
    std::filesystem::copy_file(
        source, target_mp3, std::filesystem::copy_options::overwrite_existing);
    return media_shared_file_non_empty(target_mp3);
  }

  return media_shared_transcode_audio_to_mp3_ffmpeg(source, target_mp3);
}

bool convert_acb_awb_to_mp3(const std::filesystem::path &acb,
                            const std::filesystem::path &awb,
                            const std::filesystem::path &target_mp3) {
  if (!media_shared_file_non_empty(acb) || !media_shared_file_non_empty(awb)) {
    return false;
  }

  const auto tmp_dir = media_shared_make_temp_work_dir();
  std::filesystem::path decode_acb = acb;
  std::filesystem::path decode_awb = awb;

  const bool same_parent = acb.parent_path() == awb.parent_path();
  const bool same_stem = media_shared_lower(acb.stem().string()) ==
                         media_shared_lower(awb.stem().string());
  if (!same_parent || !same_stem) {
    decode_acb = tmp_dir / acb.filename();
    decode_awb = tmp_dir / awb.filename();

    std::error_code copy_ec;
    std::filesystem::copy_file(
        acb, decode_acb, std::filesystem::copy_options::overwrite_existing,
        copy_ec);
    if (copy_ec) {
      return false;
    }

    std::filesystem::copy_file(
        awb, decode_awb, std::filesystem::copy_options::overwrite_existing,
        copy_ec);
    if (copy_ec) {
      return false;
    }

    const auto expected_awb =
        decode_acb.parent_path() / (decode_acb.stem().string() + ".awb");
    if (media_shared_lower(expected_awb.filename().string()) !=
        media_shared_lower(decode_awb.filename().string())) {
      std::filesystem::copy_file(
          decode_awb, expected_awb,
          std::filesystem::copy_options::overwrite_existing, copy_ec);
      if (copy_ec) {
        return false;
      }
      decode_awb = expected_awb;
    }
  }

  std::string awb_name_from_stub;
  uint64_t awb_size_from_stub = 0;
  if (media_shared_read_acb_stub_sidecar_awb_name(acb, awb_name_from_stub,
                                                  awb_size_from_stub)) {
    std::error_code ec;
    const auto actual_awb_size = std::filesystem::file_size(awb, ec);
    const bool awb_name_matches =
        awb_name_from_stub.empty() ||
        media_shared_lower(awb_name_from_stub) ==
            media_shared_lower(awb.filename().string());
    if (!ec && awb_name_matches && actual_awb_size == awb_size_from_stub &&
        media_shared_is_mp3_like_file(awb)) {
      if (!target_mp3.parent_path().empty()) {
        std::filesystem::create_directories(target_mp3.parent_path());
      }
      std::filesystem::copy_file(
          awb, target_mp3, std::filesystem::copy_options::overwrite_existing,
          ec);
      return !ec && media_shared_file_non_empty(target_mp3);
    }
  }

  std::vector<uint32_t> preferred_awb_entry_ids;
  (void)media_shared_collect_preferred_awb_entry_ids(decode_acb, decode_awb,
                                                     preferred_awb_entry_ids);

  const std::vector<uint32_t> *preferred_ids_ptr =
      preferred_awb_entry_ids.empty() ? nullptr : &preferred_awb_entry_ids;
  if (media_shared_transcode_audio_to_mp3_ffmpeg(decode_awb, target_mp3,
                                                 preferred_ids_ptr)) {
    return true;
  }

  return false;
}

// Not implemented. The previous implementation copied the MP3 verbatim to the
// .awb and wrote a 32-byte MaiConv-private stub as the .acb. That pair round
// -tripped only through MaiConv's own reader (read_acb_stub_sidecar_awb_name)
// and could not be loaded by the game, so reporting success was misleading.
// Throwing keeps this distinguishable from a genuine conversion failure, which
// a false return would not. See TODO.md Milestone C.
bool convert_mp3_to_acb_awb(const std::filesystem::path &source_mp3,
                            const std::filesystem::path & /*target_acb*/,
                            const std::filesystem::path & /*target_awb*/) {
  throw std::runtime_error(
      "mp3->acb+awb conversion is not implemented: " + source_mp3.string() +
      "\nThe previous implementation produced a byte-copied .awb and a stub "
      ".acb that only MaiConv could read. See TODO.md Milestone C.");
}

bool generate_silent_mp3(const std::filesystem::path &target_mp3,
                         double duration_seconds) {
  if (!std::isfinite(duration_seconds) || duration_seconds <= 0.0) {
    duration_seconds = 1.0;
  }
  if (!target_mp3.parent_path().empty()) {
    std::filesystem::create_directories(target_mp3.parent_path());
  }

  const auto mp3_encoders = media_shared_resolve_ffmpeg_mp3_encoders();
  if (mp3_encoders.empty()) {
    return false;
  }

  std::array<char, 32> duration_buf{};
  std::snprintf(duration_buf.data(), duration_buf.size(), "%.3f",
                duration_seconds);
  const std::string duration_arg = duration_buf.data();

  for (const auto &encoder : mp3_encoders) {
    media_shared_remove_file_if_exists(target_mp3);
#if defined(_WIN32)
    std::vector<std::wstring> args = {L"-y", L"-loglevel", L"error"};
    media_shared_append_audio_hwaccel_arg(args);
    args.insert(args.end(),
                {L"-f", L"lavfi", L"-i",
                 L"anullsrc=channel_layout=stereo:sample_rate=44100", L"-t",
                 media_shared_widen_ascii(duration_arg), L"-vn", L"-c:a",
                 media_shared_widen_ascii(encoder), target_mp3.wstring()});
    const bool ok = media_shared_run_ffmpeg_process(args);
#else
    std::vector<std::string> args = {"-y", "-loglevel", "error"};
    media_shared_append_audio_hwaccel_arg(args);
    args.insert(args.end(), {"-f", "lavfi", "-i",
                             "anullsrc=channel_layout=stereo:sample_rate=44100",
                             "-t", duration_arg, "-vn", "-c:a", encoder,
                             media_shared_path_to_utf8(target_mp3)});
    const bool ok = media_shared_run_ffmpeg_process(args);
#endif
    if (ok && media_shared_file_non_empty(target_mp3)) {
      return true;
    }
  }

  return false;
}

} // namespace maiconv
