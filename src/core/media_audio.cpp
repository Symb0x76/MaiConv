#include "maiconv/core/media/media_audio.hpp"

#include "media_shared.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>

namespace maiconv
{

    bool convert_audio_to_mp3(const std::filesystem::path &source,
                              const std::filesystem::path &target_mp3)
    {
        if (!media_shared_file_non_empty(source))
        {
            return false;
        }

        const std::string ext = media_shared_lower(source.extension().string());
        if (ext == ".mp3")
        {
            if (!target_mp3.parent_path().empty())
            {
                std::filesystem::create_directories(target_mp3.parent_path());
            }
            std::filesystem::copy_file(source, target_mp3,
                                       std::filesystem::copy_options::overwrite_existing);
            return media_shared_file_non_empty(target_mp3);
        }

        return media_shared_transcode_audio_to_mp3_ffmpeg(source, target_mp3);
    }

    bool convert_acb_awb_to_mp3(const std::filesystem::path &acb,
                                const std::filesystem::path &awb,
                                const std::filesystem::path &target_mp3)
    {
        if (!media_shared_file_non_empty(acb) || !media_shared_file_non_empty(awb))
        {
            return false;
        }

        const auto tmp_dir = media_shared_make_temp_work_dir();
        std::filesystem::path decode_acb = acb;
        std::filesystem::path decode_awb = awb;

        const bool same_parent = acb.parent_path() == awb.parent_path();
        const bool same_stem =
            media_shared_lower(acb.stem().string()) == media_shared_lower(awb.stem().string());
        if (!same_parent || !same_stem)
        {
            decode_acb = tmp_dir / acb.filename();
            decode_awb = tmp_dir / awb.filename();

            std::error_code copy_ec;
            std::filesystem::copy_file(acb, decode_acb,
                                       std::filesystem::copy_options::overwrite_existing,
                                       copy_ec);
            if (copy_ec)
            {
                return false;
            }

            std::filesystem::copy_file(awb, decode_awb,
                                       std::filesystem::copy_options::overwrite_existing,
                                       copy_ec);
            if (copy_ec)
            {
                return false;
            }

            const auto expected_awb = decode_acb.parent_path() / (decode_acb.stem().string() + ".awb");
            if (media_shared_lower(expected_awb.filename().string()) !=
                media_shared_lower(decode_awb.filename().string()))
            {
                std::filesystem::copy_file(decode_awb, expected_awb,
                                           std::filesystem::copy_options::overwrite_existing,
                                           copy_ec);
                if (copy_ec)
                {
                    return false;
                }
                decode_awb = expected_awb;
            }
        }

        std::string awb_name_from_stub;
        uint64_t awb_size_from_stub = 0;
        if (media_shared_read_acb_stub_sidecar_awb_name(acb, awb_name_from_stub,
                                                        awb_size_from_stub))
        {
            std::error_code ec;
            const auto actual_awb_size = std::filesystem::file_size(awb, ec);
            const bool awb_name_matches =
                awb_name_from_stub.empty() ||
                media_shared_lower(awb_name_from_stub) ==
                    media_shared_lower(awb.filename().string());
            if (!ec && awb_name_matches && actual_awb_size == awb_size_from_stub &&
                media_shared_is_mp3_like_file(awb))
            {
                if (!target_mp3.parent_path().empty())
                {
                    std::filesystem::create_directories(target_mp3.parent_path());
                }
                std::filesystem::copy_file(awb, target_mp3,
                                           std::filesystem::copy_options::overwrite_existing,
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
                                                       preferred_ids_ptr))
        {
            return true;
        }

        return false;
    }

    bool convert_mp3_to_acb_awb(const std::filesystem::path &source_mp3,
                                const std::filesystem::path &target_acb,
                                const std::filesystem::path &target_awb)
    {
        if (!media_shared_file_non_empty(source_mp3))
        {
            return false;
        }

        if (media_shared_lower(source_mp3.extension().string()) != ".mp3")
        {
            return false;
        }

        if (!target_awb.parent_path().empty())
        {
            std::filesystem::create_directories(target_awb.parent_path());
        }
        if (!target_acb.parent_path().empty())
        {
            std::filesystem::create_directories(target_acb.parent_path());
        }

        std::error_code ec;
        std::filesystem::copy_file(source_mp3, target_awb,
                                   std::filesystem::copy_options::overwrite_existing,
                                   ec);
        if (ec || !media_shared_file_non_empty(target_awb))
        {
            return false;
        }

        const auto awb_name = target_awb.filename().string();
        const auto awb_size = std::filesystem::file_size(target_awb, ec);
        if (ec)
        {
            return false;
        }

        constexpr std::array<uint8_t, 16> kAcbStubMagic = {
            static_cast<uint8_t>('M'), static_cast<uint8_t>('A'),
            static_cast<uint8_t>('I'), static_cast<uint8_t>('C'),
            static_cast<uint8_t>('O'), static_cast<uint8_t>('N'),
            static_cast<uint8_t>('V'), static_cast<uint8_t>('_'),
            static_cast<uint8_t>('A'), static_cast<uint8_t>('C'),
            static_cast<uint8_t>('B'), static_cast<uint8_t>('_'),
            static_cast<uint8_t>('S'), static_cast<uint8_t>('T'),
            static_cast<uint8_t>('U'), static_cast<uint8_t>('B')};

        auto write_u32_le = [](std::ofstream &out, uint32_t value)
        {
            const std::array<uint8_t, 4> bytes = {
                static_cast<uint8_t>(value & 0xFFU),
                static_cast<uint8_t>((value >> 8U) & 0xFFU),
                static_cast<uint8_t>((value >> 16U) & 0xFFU),
                static_cast<uint8_t>((value >> 24U) & 0xFFU)};
            out.write(reinterpret_cast<const char *>(bytes.data()),
                      static_cast<std::streamsize>(bytes.size()));
        };

        auto write_u64_le = [](std::ofstream &out, uint64_t value)
        {
            const std::array<uint8_t, 8> bytes = {
                static_cast<uint8_t>(value & 0xFFU),
                static_cast<uint8_t>((value >> 8U) & 0xFFU),
                static_cast<uint8_t>((value >> 16U) & 0xFFU),
                static_cast<uint8_t>((value >> 24U) & 0xFFU),
                static_cast<uint8_t>((value >> 32U) & 0xFFU),
                static_cast<uint8_t>((value >> 40U) & 0xFFU),
                static_cast<uint8_t>((value >> 48U) & 0xFFU),
                static_cast<uint8_t>((value >> 56U) & 0xFFU)};
            out.write(reinterpret_cast<const char *>(bytes.data()),
                      static_cast<std::streamsize>(bytes.size()));
        };

        std::ofstream out(target_acb, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            return false;
        }
        out.write(reinterpret_cast<const char *>(kAcbStubMagic.data()),
                  static_cast<std::streamsize>(kAcbStubMagic.size()));
        write_u32_le(out, 1U);
        write_u64_le(out, static_cast<uint64_t>(awb_size));
        write_u32_le(out, static_cast<uint32_t>(awb_name.size()));
        out.write(awb_name.data(), static_cast<std::streamsize>(awb_name.size()));
        out.flush();

        return out.good() && media_shared_file_non_empty(target_acb);
    }

    bool generate_silent_mp3(const std::filesystem::path &target_mp3,
                             double duration_seconds)
    {
        if (!std::isfinite(duration_seconds) || duration_seconds <= 0.0)
        {
            duration_seconds = 1.0;
        }
        if (!target_mp3.parent_path().empty())
        {
            std::filesystem::create_directories(target_mp3.parent_path());
        }

        const auto mp3_encoders = media_shared_resolve_ffmpeg_mp3_encoders();
        if (mp3_encoders.empty())
        {
            return false;
        }

        std::array<char, 32> duration_buf{};
        std::snprintf(duration_buf.data(), duration_buf.size(), "%.3f",
                      duration_seconds);
        const std::string duration_arg = duration_buf.data();

        for (const auto &encoder : mp3_encoders)
        {
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
            args.insert(args.end(),
                        {"-f", "lavfi", "-i",
                         "anullsrc=channel_layout=stereo:sample_rate=44100", "-t",
                         duration_arg, "-vn", "-c:a", encoder,
                         media_shared_path_to_utf8(target_mp3)});
            const bool ok = media_shared_run_ffmpeg_process(args);
#endif
            if (ok && media_shared_file_non_empty(target_mp3))
            {
                return true;
            }
        }

        return false;
    }

} // namespace maiconv
