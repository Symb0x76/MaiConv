#include "maiconv/core/media/media_shared.hpp"

namespace maiconv
{
    bool media_video_shared_file_non_empty(const std::filesystem::path &path);
    std::string media_video_shared_lower(const std::string &s);
    std::filesystem::path media_video_shared_make_temp_work_dir();
    bool media_video_shared_is_mp3_like_file(const std::filesystem::path &path);
    bool media_video_shared_read_acb_stub_sidecar_awb_name(
        const std::filesystem::path &acb,
        std::string &awb_name_out,
        uint64_t &awb_size_out);

    bool media_video_shared_collect_preferred_awb_entry_ids(
        const std::filesystem::path &acb,
        const std::filesystem::path &awb,
        std::vector<uint32_t> &preferred_awb_entry_ids_out);

    bool media_video_shared_transcode_audio_to_mp3_ffmpeg(
        const std::filesystem::path &source,
        const std::filesystem::path &target_mp3,
        const std::vector<uint32_t> *preferred_awb_entry_ids);

    bool media_video_shared_write_embedded_png(const std::filesystem::path &source,
                                               const std::filesystem::path &png_file);

    std::vector<std::string> media_video_shared_resolve_ffmpeg_mp3_encoders();
    void media_video_shared_remove_file_if_exists(const std::filesystem::path &path);
    std::string media_video_shared_path_to_utf8(const std::filesystem::path &path);

#if defined(_WIN32)
    std::wstring media_video_shared_widen_ascii(const std::string &value);
    void media_video_shared_append_audio_hwaccel_arg(std::vector<std::wstring> &args);
    bool media_video_shared_run_ffmpeg_process(const std::vector<std::wstring> &args);
#else
    void media_video_shared_append_audio_hwaccel_arg(std::vector<std::string> &args);
    bool media_video_shared_run_ffmpeg_process(const std::vector<std::string> &args);
#endif

    bool media_shared_file_non_empty(const std::filesystem::path &path)
    {
        return media_video_shared_file_non_empty(path);
    }

    std::string media_shared_lower(const std::string &s)
    {
        return media_video_shared_lower(s);
    }

    std::filesystem::path media_shared_make_temp_work_dir()
    {
        return media_video_shared_make_temp_work_dir();
    }

    bool media_shared_is_mp3_like_file(const std::filesystem::path &path)
    {
        return media_video_shared_is_mp3_like_file(path);
    }

    bool media_shared_read_acb_stub_sidecar_awb_name(const std::filesystem::path &acb,
                                                     std::string &awb_name_out,
                                                     uint64_t &awb_size_out)
    {
        return media_video_shared_read_acb_stub_sidecar_awb_name(
            acb, awb_name_out, awb_size_out);
    }

    bool media_shared_collect_preferred_awb_entry_ids(
        const std::filesystem::path &acb,
        const std::filesystem::path &awb,
        std::vector<uint32_t> &preferred_awb_entry_ids_out)
    {
        return media_video_shared_collect_preferred_awb_entry_ids(
            acb, awb, preferred_awb_entry_ids_out);
    }

    bool media_shared_transcode_audio_to_mp3_ffmpeg(
        const std::filesystem::path &source,
        const std::filesystem::path &target_mp3,
        const std::vector<uint32_t> *preferred_awb_entry_ids)
    {
        return media_video_shared_transcode_audio_to_mp3_ffmpeg(
            source, target_mp3, preferred_awb_entry_ids);
    }

    bool media_shared_write_embedded_png(const std::filesystem::path &source,
                                         const std::filesystem::path &png_file)
    {
        return media_video_shared_write_embedded_png(source, png_file);
    }

    std::vector<std::string> media_shared_resolve_ffmpeg_mp3_encoders()
    {
        return media_video_shared_resolve_ffmpeg_mp3_encoders();
    }

    void media_shared_remove_file_if_exists(const std::filesystem::path &path)
    {
        media_video_shared_remove_file_if_exists(path);
    }

    std::string media_shared_path_to_utf8(const std::filesystem::path &path)
    {
        return media_video_shared_path_to_utf8(path);
    }

#if defined(_WIN32)
    std::wstring media_shared_widen_ascii(const std::string &value)
    {
        return media_video_shared_widen_ascii(value);
    }

    void media_shared_append_audio_hwaccel_arg(std::vector<std::wstring> &args)
    {
        media_video_shared_append_audio_hwaccel_arg(args);
    }

    bool media_shared_run_ffmpeg_process(const std::vector<std::wstring> &args)
    {
        return media_video_shared_run_ffmpeg_process(args);
    }
#else
    void media_shared_append_audio_hwaccel_arg(std::vector<std::string> &args)
    {
        media_video_shared_append_audio_hwaccel_arg(args);
    }

    bool media_shared_run_ffmpeg_process(const std::vector<std::string> &args)
    {
        return media_video_shared_run_ffmpeg_process(args);
    }
#endif

} // namespace maiconv
