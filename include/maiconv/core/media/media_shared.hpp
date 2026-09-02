#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <string>
#include <vector>

namespace maiconv {

bool media_shared_file_non_empty(const std::filesystem::path &path);
std::string media_shared_lower(const std::string &s);
std::filesystem::path media_shared_make_temp_work_dir();

bool media_shared_is_mp3_like_file(const std::filesystem::path &path);
bool media_shared_read_acb_stub_sidecar_awb_name(
    const std::filesystem::path &acb, std::string &awb_name_out,
    uint64_t &awb_size_out);

bool media_shared_collect_preferred_awb_entry_ids(
    const std::filesystem::path &acb, const std::filesystem::path &awb,
    std::vector<uint32_t> &preferred_awb_entry_ids_out);

bool media_shared_transcode_audio_to_mp3_ffmpeg(
    const std::filesystem::path &source,
    const std::filesystem::path &target_mp3,
    const std::vector<uint32_t> *preferred_awb_entry_ids = nullptr);

bool media_shared_write_embedded_png(const std::filesystem::path &source,
                                     const std::filesystem::path &png_file);

std::vector<std::string> media_shared_resolve_ffmpeg_mp3_encoders();
void media_shared_remove_file_if_exists(const std::filesystem::path &path);
std::string media_shared_path_to_utf8(const std::filesystem::path &path);

std::vector<std::string> media_shared_resolve_ffmpeg_h264_encoders();
std::vector<std::string> media_shared_resolve_ffmpeg_vp9_encoders();
bool media_shared_write_binary_file(const std::filesystem::path &path,
                                    const std::vector<uint8_t> &data);
bool media_shared_read_exact(std::istream &in, uint8_t *out, std::size_t size);
uint16_t media_shared_read_u16_be(const uint8_t *p);
uint16_t media_shared_read_u16_le(const uint8_t *p);
uint32_t media_shared_read_u32_be(const uint8_t *p);

#if defined(_WIN32)
std::wstring media_shared_widen_ascii(const std::string &value);
void media_shared_append_audio_hwaccel_arg(std::vector<std::wstring> &args);
bool media_shared_run_ffmpeg_process(const std::vector<std::wstring> &args);
void media_shared_append_hwaccel_arg(std::vector<std::wstring> &args);
bool media_shared_run_ffmpeg_capture_stdout(
    const std::vector<std::wstring> &args, std::vector<uint8_t> &stdout_bytes);
bool media_shared_run_ffmpeg_feed_stdin(
    const std::vector<std::wstring> &args,
    const std::vector<uint8_t> &stdin_bytes);
#else
void media_shared_append_audio_hwaccel_arg(std::vector<std::string> &args);
bool media_shared_run_ffmpeg_process(const std::vector<std::string> &args);
void media_shared_append_hwaccel_arg(std::vector<std::string> &args);
bool media_shared_run_ffmpeg_capture_stdout(
    const std::vector<std::string> &args, std::vector<uint8_t> &stdout_bytes);
bool media_shared_run_ffmpeg_feed_stdin(
    const std::vector<std::string> &args,
    const std::vector<uint8_t> &stdin_bytes);
#endif

} // namespace maiconv