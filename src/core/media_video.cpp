#include "maiconv/core/media/media_video.hpp"

#include "maiconv/core/io.hpp"

namespace maiconv
{
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

namespace maiconv
{
    namespace
    {

        constexpr std::array<uint8_t, 16> kAcbStubMagic = {
            static_cast<uint8_t>('M'), static_cast<uint8_t>('A'),
            static_cast<uint8_t>('I'), static_cast<uint8_t>('C'),
            static_cast<uint8_t>('O'), static_cast<uint8_t>('N'),
            static_cast<uint8_t>('V'), static_cast<uint8_t>('_'),
            static_cast<uint8_t>('A'), static_cast<uint8_t>('C'),
            static_cast<uint8_t>('B'), static_cast<uint8_t>('_'),
            static_cast<uint8_t>('S'), static_cast<uint8_t>('T'),
            static_cast<uint8_t>('U'), static_cast<uint8_t>('B')};

        bool is_mp3_like_file(const std::filesystem::path &path)
        {
            std::ifstream in(path, std::ios::binary);
            if (!in)
            {
                return false;
            }

            std::array<uint8_t, 3> head{};
            in.read(reinterpret_cast<char *>(head.data()),
                    static_cast<std::streamsize>(head.size()));
            if (in.gcount() < 2)
            {
                return false;
            }

            if (in.gcount() == 3 && head[0] == static_cast<uint8_t>('I') &&
                head[1] == static_cast<uint8_t>('D') &&
                head[2] == static_cast<uint8_t>('3'))
            {
                return true;
            }

            return head[0] == 0xFFU && (head[1] & 0xE0U) == 0xE0U;
        }

        bool read_acb_stub_sidecar_awb_name(const std::filesystem::path &acb,
                                            std::string &awb_name_out,
                                            uint64_t &awb_size_out)
        {
            std::ifstream in(acb, std::ios::binary);
            if (!in)
            {
                return false;
            }

            std::array<uint8_t, 16> magic{};
            in.read(reinterpret_cast<char *>(magic.data()),
                    static_cast<std::streamsize>(magic.size()));
            if (in.gcount() != static_cast<std::streamsize>(magic.size()) ||
                magic != kAcbStubMagic)
            {
                return false;
            }

            std::array<uint8_t, 4> u32{};
            in.read(reinterpret_cast<char *>(u32.data()),
                    static_cast<std::streamsize>(u32.size()));
            if (in.gcount() != static_cast<std::streamsize>(u32.size()))
            {
                return false;
            }
            const uint32_t version = static_cast<uint32_t>(u32[0]) |
                                     (static_cast<uint32_t>(u32[1]) << 8U) |
                                     (static_cast<uint32_t>(u32[2]) << 16U) |
                                     (static_cast<uint32_t>(u32[3]) << 24U);
            if (version != 1U)
            {
                return false;
            }

            std::array<uint8_t, 8> u64{};
            in.read(reinterpret_cast<char *>(u64.data()),
                    static_cast<std::streamsize>(u64.size()));
            if (in.gcount() != static_cast<std::streamsize>(u64.size()))
            {
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

            in.read(reinterpret_cast<char *>(u32.data()),
                    static_cast<std::streamsize>(u32.size()));
            if (in.gcount() != static_cast<std::streamsize>(u32.size()))
            {
                return false;
            }
            const uint32_t name_size = static_cast<uint32_t>(u32[0]) |
                                       (static_cast<uint32_t>(u32[1]) << 8U) |
                                       (static_cast<uint32_t>(u32[2]) << 16U) |
                                       (static_cast<uint32_t>(u32[3]) << 24U);
            if (name_size == 0U || name_size > 1024U)
            {
                return false;
            }

            std::string awb_name(name_size, '\0');
            in.read(awb_name.data(), static_cast<std::streamsize>(awb_name.size()));
            if (in.gcount() != static_cast<std::streamsize>(awb_name.size()))
            {
                return false;
            }

            awb_name_out = std::move(awb_name);
            return true;
        }

        uint32_t read_u32_be(const uint8_t *p)
        {
            return (static_cast<uint32_t>(p[0]) << 24U) |
                   (static_cast<uint32_t>(p[1]) << 16U) |
                   (static_cast<uint32_t>(p[2]) << 8U) | static_cast<uint32_t>(p[3]);
        }

        uint32_t read_u32_le(const uint8_t *p)
        {
            return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8U) |
                   (static_cast<uint32_t>(p[2]) << 16U) |
                   (static_cast<uint32_t>(p[3]) << 24U);
        }

        uint16_t read_u16_le(const uint8_t *p)
        {
            return static_cast<uint16_t>(p[0]) |
                   static_cast<uint16_t>(static_cast<uint16_t>(p[1]) << 8U);
        }

        uint16_t read_u16_be(const uint8_t *p)
        {
            return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8U) |
                                         static_cast<uint16_t>(p[1]));
        }

        uint64_t read_u64_be(const uint8_t *p)
        {
            return (static_cast<uint64_t>(p[0]) << 56U) |
                   (static_cast<uint64_t>(p[1]) << 48U) |
                   (static_cast<uint64_t>(p[2]) << 40U) |
                   (static_cast<uint64_t>(p[3]) << 32U) |
                   (static_cast<uint64_t>(p[4]) << 24U) |
                   (static_cast<uint64_t>(p[5]) << 16U) |
                   (static_cast<uint64_t>(p[6]) << 8U) | static_cast<uint64_t>(p[7]);
        }

        uint64_t read_u64_le(const uint8_t *p)
        {
            return static_cast<uint64_t>(p[0]) | (static_cast<uint64_t>(p[1]) << 8U) |
                   (static_cast<uint64_t>(p[2]) << 16U) |
                   (static_cast<uint64_t>(p[3]) << 24U) |
                   (static_cast<uint64_t>(p[4]) << 32U) |
                   (static_cast<uint64_t>(p[5]) << 40U) |
                   (static_cast<uint64_t>(p[6]) << 48U) |
                   (static_cast<uint64_t>(p[7]) << 56U);
        }

        int8_t read_i8(const uint8_t *p) { return static_cast<int8_t>(p[0]); }

        int16_t read_i16_be(const uint8_t *p)
        {
            return static_cast<int16_t>(read_u16_be(p));
        }

        int32_t read_i32_be(const uint8_t *p)
        {
            return static_cast<int32_t>(read_u32_be(p));
        }

        int64_t read_i64_be(const uint8_t *p)
        {
            return static_cast<int64_t>((static_cast<uint64_t>(p[0]) << 56U) |
                                        (static_cast<uint64_t>(p[1]) << 48U) |
                                        (static_cast<uint64_t>(p[2]) << 40U) |
                                        (static_cast<uint64_t>(p[3]) << 32U) |
                                        (static_cast<uint64_t>(p[4]) << 24U) |
                                        (static_cast<uint64_t>(p[5]) << 16U) |
                                        (static_cast<uint64_t>(p[6]) << 8U) |
                                        static_cast<uint64_t>(p[7]));
        }

        bool file_non_empty(const std::filesystem::path &path)
        {
            return std::filesystem::exists(path) &&
                   std::filesystem::is_regular_file(path) &&
                   std::filesystem::file_size(path) > 0;
        }

        class TempWorkspacePool
        {
        public:
            TempWorkspacePool()
            {
                const auto now =
                    std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::high_resolution_clock::now().time_since_epoch())
                        .count();
                root_ = std::filesystem::temp_directory_path() /
                        ("maiconv_media_pool_" + std::to_string(now));
                std::filesystem::create_directories(root_);
            }

            ~TempWorkspacePool()
            {
                std::error_code ec;
                std::filesystem::remove_all(root_, ec);
            }

            std::filesystem::path make_workspace()
            {
                const auto id = counter_.fetch_add(1, std::memory_order_relaxed);
                const auto dir = root_ / ("workspace_" + std::to_string(id));
                std::filesystem::create_directories(dir);
                return dir;
            }

        private:
            std::filesystem::path root_;
            std::atomic<unsigned long long> counter_{0};
        };

        TempWorkspacePool &temp_workspace_pool()
        {
            static TempWorkspacePool pool;
            return pool;
        }

        std::filesystem::path make_temp_work_dir()
        {
            return temp_workspace_pool().make_workspace();
        }

        [[maybe_unused]] std::string path_to_utf8(const std::filesystem::path &path)
        {
#if defined(_WIN32)
#if defined(__cpp_char8_t)
            const std::u8string value = path.u8string();
            std::string out;
            out.reserve(value.size());
            for (const char8_t ch : value)
            {
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

        std::optional<std::string> read_non_empty_env(const char *name)
        {
#if defined(_WIN32)
            char *value = nullptr;
            std::size_t value_len = 0;
            const errno_t rc = _dupenv_s(&value, &value_len, name);
            if (rc != 0 || value == nullptr || value[0] == '\0')
            {
                if (value != nullptr)
                {
                    std::free(value);
                }
                return std::nullopt;
            }
            std::string out(value);
            std::free(value);
            return out;
#else
            const char *value = std::getenv(name);
            if (value == nullptr || value[0] == '\0')
            {
                return std::nullopt;
            }
            return std::string(value);
#endif
        }

#if defined(_WIN32)
        std::optional<std::wstring> read_non_empty_wenv(const wchar_t *name)
        {
            wchar_t *value = nullptr;
            std::size_t value_len = 0;
            const errno_t rc = _wdupenv_s(&value, &value_len, name);
            if (rc != 0 || value == nullptr || value[0] == L'\0')
            {
                if (value != nullptr)
                {
                    std::free(value);
                }
                return std::nullopt;
            }
            std::wstring out(value);
            std::free(value);
            return out;
        }
#endif

        std::optional<std::string>
        normalize_hwaccel_env_value(const std::string &value)
        {
            const std::string normalized = lower(value);
            if (normalized == "0" || normalized == "false" || normalized == "off" ||
                normalized == "none")
            {
                return std::nullopt;
            }
            if (normalized == "1" || normalized == "true" || normalized == "on" ||
                normalized == "yes")
            {
                return std::string("auto");
            }
            return value;
        }

        std::optional<std::string> resolve_ffmpeg_hwaccel()
        {
            const auto value = read_non_empty_env("MAICONV_FFMPEG_HWACCEL");
            if (value.has_value())
            {
                return normalize_hwaccel_env_value(*value);
            }

            const auto gpu_mode = read_non_empty_env("MAICONV_FFMPEG_GPU");
            if (!gpu_mode.has_value())
            {
                return std::nullopt;
            }
            const std::string normalized = lower(*gpu_mode);
            if (normalized == "0" || normalized == "false" || normalized == "off" ||
                normalized == "no")
            {
                return std::nullopt;
            }
            return std::string("auto");
        }

        std::optional<std::string> resolve_ffmpeg_audio_hwaccel()
        {
            const auto audio_value = read_non_empty_env("MAICONV_FFMPEG_AUDIO_HWACCEL");
            if (audio_value.has_value())
            {
                return normalize_hwaccel_env_value(*audio_value);
            }
            return resolve_ffmpeg_hwaccel();
        }

        void append_unique_string(std::vector<std::string> &out,
                                  const std::string &value)
        {
            if (value.empty())
            {
                return;
            }
            if (std::find(out.begin(), out.end(), value) == out.end())
            {
                out.push_back(value);
            }
        }

        [[maybe_unused]] void append_audio_hwaccel_arg(std::vector<std::string> &args)
        {
            const auto hwaccel = resolve_ffmpeg_audio_hwaccel();
            if (!hwaccel.has_value())
            {
                return;
            }
            args.push_back("-hwaccel");
            args.push_back(*hwaccel);
        }

        std::vector<std::string> resolve_ffmpeg_mp3_encoders()
        {
            std::vector<std::string> encoders;
            if (const auto value = read_non_empty_env("MAICONV_FFMPEG_MP3_ENCODER");
                value.has_value())
            {
                append_unique_string(encoders, *value);
                return encoders;
            }

            append_unique_string(encoders, "libmp3lame");
            append_unique_string(encoders, "mp3");
            append_unique_string(encoders, "libshine");
#if defined(_WIN32)
            append_unique_string(encoders, "mp3_mf");
#endif
            return encoders;
        }

        std::vector<std::string> resolve_ffmpeg_h264_encoders()
        {
            std::vector<std::string> encoders;
            if (const auto value = read_non_empty_env("MAICONV_FFMPEG_H264_ENCODER");
                value.has_value())
            {
                append_unique_string(encoders, *value);
                return encoders;
            }

            if (resolve_ffmpeg_hwaccel().has_value())
            {
                append_unique_string(encoders, "h264_nvenc");
                append_unique_string(encoders, "h264_qsv");
                append_unique_string(encoders, "h264_amf");
            }
            append_unique_string(encoders, "libx264");
            return encoders;
        }

        std::vector<std::string> resolve_ffmpeg_vp9_encoders()
        {
            std::vector<std::string> encoders;
            if (const auto value = read_non_empty_env("MAICONV_FFMPEG_VP9_ENCODER");
                value.has_value())
            {
                append_unique_string(encoders, *value);
                return encoders;
            }

            if (resolve_ffmpeg_hwaccel().has_value())
            {
                append_unique_string(encoders, "vp9_qsv");
            }
            append_unique_string(encoders, "libvpx-vp9");
            return encoders;
        }

        [[maybe_unused]] void append_hwaccel_arg(std::vector<std::string> &args)
        {
            const auto hwaccel = resolve_ffmpeg_hwaccel();
            if (!hwaccel.has_value())
            {
                return;
            }
            args.push_back("-hwaccel");
            args.push_back(*hwaccel);
        }

        void remove_file_if_exists(const std::filesystem::path &path)
        {
            std::error_code ec;
            std::filesystem::remove(path, ec);
        }

#if defined(_WIN32)
        std::wstring resolve_ffmpeg_executable();

        std::wstring widen_ascii(const std::string &value)
        {
            return std::wstring(value.begin(), value.end());
        }

        void append_hwaccel_arg(std::vector<std::wstring> &args)
        {
            const auto hwaccel = resolve_ffmpeg_hwaccel();
            if (!hwaccel.has_value())
            {
                return;
            }
            args.push_back(L"-hwaccel");
            args.push_back(widen_ascii(*hwaccel));
        }

        void append_audio_hwaccel_arg(std::vector<std::wstring> &args)
        {
            const auto hwaccel = resolve_ffmpeg_audio_hwaccel();
            if (!hwaccel.has_value())
            {
                return;
            }
            args.push_back(L"-hwaccel");
            args.push_back(widen_ascii(*hwaccel));
        }

        std::wstring quote_windows_argument(const std::wstring &arg)
        {
            if (arg.empty())
            {
                return L"\"\"";
            }
            if (arg.find_first_of(L" \t\n\v\"") == std::wstring::npos)
            {
                return arg;
            }

            std::wstring out;
            out.reserve(arg.size() + 2);
            out.push_back(L'"');

            std::size_t backslashes = 0;
            for (const wchar_t ch : arg)
            {
                if (ch == L'\\')
                {
                    ++backslashes;
                    continue;
                }
                if (ch == L'"')
                {
                    out.append(backslashes * 2 + 1, L'\\');
                    out.push_back(L'"');
                    backslashes = 0;
                    continue;
                }
                if (backslashes > 0)
                {
                    out.append(backslashes, L'\\');
                    backslashes = 0;
                }
                out.push_back(ch);
            }
            if (backslashes > 0)
            {
                out.append(backslashes * 2, L'\\');
            }
            out.push_back(L'"');
            return out;
        }

        std::wstring build_windows_command_line(const std::vector<std::wstring> &args)
        {
            std::wstring command_line =
                quote_windows_argument(resolve_ffmpeg_executable());
            for (const auto &arg : args)
            {
                command_line.push_back(L' ');
                command_line += quote_windows_argument(arg);
            }
            return command_line;
        }

        bool wait_process_success(HANDLE process_handle, HANDLE thread_handle)
        {
            const DWORD wait_rc = WaitForSingleObject(process_handle, INFINITE);
            DWORD exit_code = 1;
            if (wait_rc == WAIT_OBJECT_0)
            {
                GetExitCodeProcess(process_handle, &exit_code);
            }

            CloseHandle(thread_handle);
            CloseHandle(process_handle);
            return wait_rc == WAIT_OBJECT_0 && exit_code == 0;
        }

        std::wstring resolve_ffmpeg_executable()
        {
            static std::once_flag once;
            static std::wstring cached;
            std::call_once(once, []()
                           {
    if (const auto env_path = read_non_empty_wenv(L"MAICONV_FFMPEG");
        env_path.has_value()) {
      cached = *env_path;
      return;
    }

    const wchar_t *file = L"ffmpeg.exe";
    const DWORD needed =
        SearchPathW(nullptr, file, nullptr, 0, nullptr, nullptr);
    if (needed > 0) {
      std::wstring buffer(static_cast<std::size_t>(needed), L'\0');
      const DWORD written =
          SearchPathW(nullptr, file, nullptr, needed, buffer.data(), nullptr);
      if (written > 0) {
        buffer.resize(static_cast<std::size_t>(written));
        cached = std::move(buffer);
        return;
      }
    }

    cached = file; });
            return cached;
        }

        [[maybe_unused]] bool
        run_ffmpeg_process(const std::vector<std::wstring> &args)
        {
            if (args.empty())
            {
                return false;
            }

            std::wstring command_line = build_windows_command_line(args);
            STARTUPINFOW startup{};
            startup.cb = sizeof(startup);
            startup.dwFlags = STARTF_USESHOWWINDOW;
            startup.wShowWindow = SW_HIDE;

            PROCESS_INFORMATION process{};
            std::wstring mutable_command_line = command_line;
            const BOOL created = CreateProcessW(nullptr, mutable_command_line.data(),
                                                nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                                                nullptr, nullptr, &startup, &process);
            if (!created)
            {
                return false;
            }

            return wait_process_success(process.hProcess, process.hThread);
        }

        bool run_ffmpeg_capture_stdout(const std::vector<std::wstring> &args,
                                       std::vector<uint8_t> &stdout_bytes)
        {
            if (args.empty())
            {
                return false;
            }

            stdout_bytes.clear();

            SECURITY_ATTRIBUTES sa{};
            sa.nLength = sizeof(sa);
            sa.bInheritHandle = TRUE;

            HANDLE out_read = nullptr;
            HANDLE out_write = nullptr;
            if (!CreatePipe(&out_read, &out_write, &sa, 0))
            {
                return false;
            }
            if (!SetHandleInformation(out_read, HANDLE_FLAG_INHERIT, 0))
            {
                CloseHandle(out_read);
                CloseHandle(out_write);
                return false;
            }

            HANDLE in_null =
                CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            HANDLE err_null =
                CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (in_null == INVALID_HANDLE_VALUE || err_null == INVALID_HANDLE_VALUE)
            {
                if (in_null != INVALID_HANDLE_VALUE)
                {
                    CloseHandle(in_null);
                }
                if (err_null != INVALID_HANDLE_VALUE)
                {
                    CloseHandle(err_null);
                }
                CloseHandle(out_read);
                CloseHandle(out_write);
                return false;
            }

            STARTUPINFOW startup{};
            startup.cb = sizeof(startup);
            startup.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
            startup.wShowWindow = SW_HIDE;
            startup.hStdInput = in_null;
            startup.hStdOutput = out_write;
            startup.hStdError = err_null;

            std::wstring command_line = build_windows_command_line(args);
            std::wstring mutable_command_line = command_line;
            PROCESS_INFORMATION process{};
            const BOOL created = CreateProcessW(nullptr, mutable_command_line.data(),
                                                nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                                                nullptr, nullptr, &startup, &process);

            CloseHandle(out_write);
            CloseHandle(in_null);
            CloseHandle(err_null);

            if (!created)
            {
                CloseHandle(out_read);
                return false;
            }

            std::array<uint8_t, 32768> buffer{};
            DWORD read_count = 0;
            while (ReadFile(out_read, buffer.data(), static_cast<DWORD>(buffer.size()),
                            &read_count, nullptr) &&
                   read_count > 0)
            {
                stdout_bytes.insert(stdout_bytes.end(), buffer.begin(),
                                    buffer.begin() +
                                        static_cast<std::ptrdiff_t>(read_count));
            }
            CloseHandle(out_read);

            const bool ok = wait_process_success(process.hProcess, process.hThread);
            return ok;
        }

        bool run_ffmpeg_feed_stdin(const std::vector<std::wstring> &args,
                                   const std::vector<uint8_t> &stdin_bytes)
        {
            if (args.empty())
            {
                return false;
            }

            SECURITY_ATTRIBUTES sa{};
            sa.nLength = sizeof(sa);
            sa.bInheritHandle = TRUE;

            HANDLE in_read = nullptr;
            HANDLE in_write = nullptr;
            if (!CreatePipe(&in_read, &in_write, &sa, 0))
            {
                return false;
            }
            if (!SetHandleInformation(in_write, HANDLE_FLAG_INHERIT, 0))
            {
                CloseHandle(in_read);
                CloseHandle(in_write);
                return false;
            }

            HANDLE out_null =
                CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            HANDLE err_null =
                CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (out_null == INVALID_HANDLE_VALUE || err_null == INVALID_HANDLE_VALUE)
            {
                if (out_null != INVALID_HANDLE_VALUE)
                {
                    CloseHandle(out_null);
                }
                if (err_null != INVALID_HANDLE_VALUE)
                {
                    CloseHandle(err_null);
                }
                CloseHandle(in_read);
                CloseHandle(in_write);
                return false;
            }

            STARTUPINFOW startup{};
            startup.cb = sizeof(startup);
            startup.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
            startup.wShowWindow = SW_HIDE;
            startup.hStdInput = in_read;
            startup.hStdOutput = out_null;
            startup.hStdError = err_null;

            std::wstring command_line = build_windows_command_line(args);
            std::wstring mutable_command_line = command_line;
            PROCESS_INFORMATION process{};
            const BOOL created = CreateProcessW(nullptr, mutable_command_line.data(),
                                                nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                                                nullptr, nullptr, &startup, &process);

            CloseHandle(in_read);
            CloseHandle(out_null);
            CloseHandle(err_null);

            if (!created)
            {
                CloseHandle(in_write);
                return false;
            }

            bool write_ok = true;
            std::size_t offset = 0;
            while (offset < stdin_bytes.size())
            {
                const auto remaining = stdin_bytes.size() - offset;
                const DWORD chunk =
                    static_cast<DWORD>(std::min<std::size_t>(remaining, 1U << 20));
                DWORD written = 0;
                if (!WriteFile(in_write,
                               stdin_bytes.data() + static_cast<std::ptrdiff_t>(offset),
                               chunk, &written, nullptr) ||
                    written == 0)
                {
                    write_ok = false;
                    break;
                }
                offset += static_cast<std::size_t>(written);
            }
            CloseHandle(in_write);

            const bool run_ok = wait_process_success(process.hProcess, process.hThread);
            return write_ok && run_ok;
        }
#else
        std::string resolve_ffmpeg_executable()
        {
            static std::once_flag once;
            static std::string cached;
            std::call_once(once, []()
                           {
    if (const auto env_path = read_non_empty_env("MAICONV_FFMPEG");
        env_path.has_value()) {
      cached = *env_path;
      return;
    }
    cached = "ffmpeg"; });
            return cached;
        }

        [[maybe_unused]] bool run_ffmpeg_process(const std::vector<std::string> &args)
        {
            if (args.empty())
            {
                return false;
            }

            std::vector<std::string> argv_storage;
            argv_storage.reserve(args.size() + 1);
            argv_storage.push_back(resolve_ffmpeg_executable());
            argv_storage.insert(argv_storage.end(), args.begin(), args.end());

            std::vector<char *> argv;
            argv.reserve(argv_storage.size() + 1);
            for (auto &token : argv_storage)
            {
                argv.push_back(token.data());
            }
            argv.push_back(nullptr);

            const std::string &executable = argv_storage.front();
            const bool has_separator = executable.find('/') != std::string::npos;

            pid_t pid = -1;
            const int spawn_rc = has_separator
                                     ? posix_spawn(&pid, executable.c_str(), nullptr,
                                                   nullptr, argv.data(), ::environ)
                                     : posix_spawnp(&pid, executable.c_str(), nullptr,
                                                    nullptr, argv.data(), ::environ);
            if (spawn_rc != 0)
            {
                return false;
            }

            int status = 0;
            int wait_rc = 0;
            do
            {
                wait_rc = waitpid(pid, &status, 0);
            } while (wait_rc == -1 && errno == EINTR);
            if (wait_rc == -1)
            {
                return false;
            }

            return WIFEXITED(status) && WEXITSTATUS(status) == 0;
        }

        bool run_ffmpeg_capture_stdout(const std::vector<std::string> &args,
                                       std::vector<uint8_t> &stdout_bytes)
        {
            if (args.empty())
            {
                return false;
            }
            stdout_bytes.clear();

            int out_pipe[2] = {-1, -1};
            if (pipe(out_pipe) != 0)
            {
                return false;
            }

            const int in_null = open("/dev/null", O_RDONLY);
            const int err_null = open("/dev/null", O_WRONLY);
            if (in_null < 0 || err_null < 0)
            {
                if (in_null >= 0)
                {
                    close(in_null);
                }
                if (err_null >= 0)
                {
                    close(err_null);
                }
                close(out_pipe[0]);
                close(out_pipe[1]);
                return false;
            }

            std::vector<std::string> argv_storage;
            argv_storage.reserve(args.size() + 1);
            argv_storage.push_back(resolve_ffmpeg_executable());
            argv_storage.insert(argv_storage.end(), args.begin(), args.end());

            std::vector<char *> argv;
            argv.reserve(argv_storage.size() + 1);
            for (auto &token : argv_storage)
            {
                argv.push_back(token.data());
            }
            argv.push_back(nullptr);

            posix_spawn_file_actions_t actions;
            posix_spawn_file_actions_init(&actions);
            posix_spawn_file_actions_adddup2(&actions, in_null, STDIN_FILENO);
            posix_spawn_file_actions_adddup2(&actions, out_pipe[1], STDOUT_FILENO);
            posix_spawn_file_actions_adddup2(&actions, err_null, STDERR_FILENO);
            posix_spawn_file_actions_addclose(&actions, out_pipe[0]);
            posix_spawn_file_actions_addclose(&actions, out_pipe[1]);
            posix_spawn_file_actions_addclose(&actions, in_null);
            posix_spawn_file_actions_addclose(&actions, err_null);

            const std::string &executable = argv_storage.front();
            const bool has_separator = executable.find('/') != std::string::npos;
            pid_t pid = -1;
            const int spawn_rc = has_separator
                                     ? posix_spawn(&pid, executable.c_str(), &actions,
                                                   nullptr, argv.data(), ::environ)
                                     : posix_spawnp(&pid, executable.c_str(), &actions,
                                                    nullptr, argv.data(), ::environ);
            posix_spawn_file_actions_destroy(&actions);
            close(in_null);
            close(err_null);
            close(out_pipe[1]);

            if (spawn_rc != 0)
            {
                close(out_pipe[0]);
                return false;
            }

            std::array<uint8_t, 32768> buffer{};
            while (true)
            {
                const ssize_t n = read(out_pipe[0], buffer.data(), buffer.size());
                if (n == 0)
                {
                    break;
                }
                if (n < 0)
                {
                    if (errno == EINTR)
                    {
                        continue;
                    }
                    close(out_pipe[0]);
                    return false;
                }
                stdout_bytes.insert(stdout_bytes.end(), buffer.begin(),
                                    buffer.begin() + static_cast<std::ptrdiff_t>(n));
            }
            close(out_pipe[0]);

            int status = 0;
            int wait_rc = 0;
            do
            {
                wait_rc = waitpid(pid, &status, 0);
            } while (wait_rc == -1 && errno == EINTR);
            if (wait_rc == -1)
            {
                return false;
            }

            return WIFEXITED(status) && WEXITSTATUS(status) == 0;
        }

        bool run_ffmpeg_feed_stdin(const std::vector<std::string> &args,
                                   const std::vector<uint8_t> &stdin_bytes)
        {
            if (args.empty())
            {
                return false;
            }

            int in_pipe[2] = {-1, -1};
            if (pipe(in_pipe) != 0)
            {
                return false;
            }

            const int out_null = open("/dev/null", O_WRONLY);
            const int err_null = open("/dev/null", O_WRONLY);
            if (out_null < 0 || err_null < 0)
            {
                if (out_null >= 0)
                {
                    close(out_null);
                }
                if (err_null >= 0)
                {
                    close(err_null);
                }
                close(in_pipe[0]);
                close(in_pipe[1]);
                return false;
            }

            std::vector<std::string> argv_storage;
            argv_storage.reserve(args.size() + 1);
            argv_storage.push_back(resolve_ffmpeg_executable());
            argv_storage.insert(argv_storage.end(), args.begin(), args.end());

            std::vector<char *> argv;
            argv.reserve(argv_storage.size() + 1);
            for (auto &token : argv_storage)
            {
                argv.push_back(token.data());
            }
            argv.push_back(nullptr);

            posix_spawn_file_actions_t actions;
            posix_spawn_file_actions_init(&actions);
            posix_spawn_file_actions_adddup2(&actions, in_pipe[0], STDIN_FILENO);
            posix_spawn_file_actions_adddup2(&actions, out_null, STDOUT_FILENO);
            posix_spawn_file_actions_adddup2(&actions, err_null, STDERR_FILENO);
            posix_spawn_file_actions_addclose(&actions, in_pipe[0]);
            posix_spawn_file_actions_addclose(&actions, in_pipe[1]);
            posix_spawn_file_actions_addclose(&actions, out_null);
            posix_spawn_file_actions_addclose(&actions, err_null);

            const std::string &executable = argv_storage.front();
            const bool has_separator = executable.find('/') != std::string::npos;
            pid_t pid = -1;
            const int spawn_rc = has_separator
                                     ? posix_spawn(&pid, executable.c_str(), &actions,
                                                   nullptr, argv.data(), ::environ)
                                     : posix_spawnp(&pid, executable.c_str(), &actions,
                                                    nullptr, argv.data(), ::environ);
            posix_spawn_file_actions_destroy(&actions);
            close(in_pipe[0]);
            close(out_null);
            close(err_null);

            if (spawn_rc != 0)
            {
                close(in_pipe[1]);
                return false;
            }

            bool write_ok = true;
            std::size_t offset = 0;
            while (offset < stdin_bytes.size())
            {
                const auto remaining = stdin_bytes.size() - offset;
                const size_t chunk = std::min<std::size_t>(remaining, 1U << 20);
                const ssize_t written =
                    write(in_pipe[1],
                          stdin_bytes.data() + static_cast<std::ptrdiff_t>(offset), chunk);
                if (written < 0)
                {
                    if (errno == EINTR)
                    {
                        continue;
                    }
                    write_ok = false;
                    break;
                }
                if (written == 0)
                {
                    write_ok = false;
                    break;
                }
                offset += static_cast<std::size_t>(written);
            }
            close(in_pipe[1]);

            int status = 0;
            int wait_rc = 0;
            do
            {
                wait_rc = waitpid(pid, &status, 0);
            } while (wait_rc == -1 && errno == EINTR);
            if (wait_rc == -1)
            {
                return false;
            }

            return write_ok && WIFEXITED(status) && WEXITSTATUS(status) == 0;
        }
#endif

        bool read_exact(std::istream &in, uint8_t *out, std::size_t size)
        {
            if (size == 0)
            {
                return true;
            }
            in.read(reinterpret_cast<char *>(out), static_cast<std::streamsize>(size));
            return static_cast<std::size_t>(in.gcount()) == size;
        }

        template <std::size_t N>
        bool starts_with(const std::vector<uint8_t> &data,
                         const std::array<uint8_t, N> &sig)
        {
            return data.size() >= sig.size() &&
                   std::equal(sig.begin(), sig.end(), data.begin());
        }

        bool write_embedded_png(const std::filesystem::path &source,
                                const std::filesystem::path &png_file)
        {
            std::ifstream in(source, std::ios::binary);
            if (!in)
            {
                return false;
            }

            in.seekg(0, std::ios::end);
            const auto size = in.tellg();
            if (size <= 0)
            {
                return false;
            }
            in.seekg(0, std::ios::beg);

            std::vector<uint8_t> bytes(static_cast<std::size_t>(size));
            in.read(reinterpret_cast<char *>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size()));
            if (!in)
            {
                return false;
            }

            constexpr std::array<uint8_t, 8> kPngSig = {0x89U, 0x50U, 0x4EU, 0x47U,
                                                        0x0DU, 0x0AU, 0x1AU, 0x0AU};

            for (std::size_t start = 0; start + kPngSig.size() < bytes.size(); ++start)
            {
                if (!std::equal(kPngSig.begin(), kPngSig.end(),
                                bytes.begin() + static_cast<std::ptrdiff_t>(start)))
                {
                    continue;
                }

                std::size_t chunk = start + kPngSig.size();
                while (chunk + 8 <= bytes.size())
                {
                    const uint32_t length = read_u32_be(bytes.data() + chunk);
                    const std::size_t data_start = chunk + 8;
                    const std::size_t data_end =
                        data_start + static_cast<std::size_t>(length);
                    const std::size_t crc_end = data_end + 4;
                    if (crc_end > bytes.size())
                    {
                        break;
                    }

                    const std::string type(
                        reinterpret_cast<const char *>(bytes.data() + chunk + 4), 4);
                    if (type == "IEND")
                    {
                        if (!png_file.parent_path().empty())
                        {
                            std::filesystem::create_directories(png_file.parent_path());
                        }
                        std::ofstream out(png_file, std::ios::binary | std::ios::trunc);
                        if (!out)
                        {
                            return false;
                        }
                        out.write(reinterpret_cast<const char *>(bytes.data() + start),
                                  static_cast<std::streamsize>(crc_end - start));
                        out.flush();
                        return out.good() && file_non_empty(png_file);
                    }

                    chunk = crc_end;
                }
            }

            return false;
        }

        struct UsmKeys
        {
            std::array<uint8_t, 0x40> video{};
        };

        UsmKeys make_usm_keys(uint64_t key_num)
        {
            std::array<uint8_t, 8> cipher{};
            for (int i = 0; i < 8; ++i)
            {
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
            for (std::size_t i = 0; i < 0x20; ++i)
            {
                out.video[i] = key[i];
                out.video[0x20 + i] = static_cast<uint8_t>(key[i] ^ 0xFFU);
            }
            return out;
        }

        std::vector<uint8_t>
        decrypt_usm_video_packet(const std::vector<uint8_t> &packet,
                                 const std::array<uint8_t, 0x40> &video_key)
        {
            std::vector<uint8_t> data = packet;
            if (data.size() < 0x40 + 0x200)
            {
                return data;
            }

            std::array<uint8_t, 0x40> rolling = video_key;
            const std::size_t encrypted_size = data.size() - 0x40;

            for (std::size_t i = 0x100; i < encrypted_size; ++i)
            {
                const std::size_t packet_index = 0x40 + i;
                const std::size_t key_index = 0x20 + (i % 0x20);
                data[packet_index] =
                    static_cast<uint8_t>(data[packet_index] ^ rolling[key_index]);
                rolling[key_index] =
                    static_cast<uint8_t>(data[packet_index] ^ video_key[key_index]);
            }

            for (std::size_t i = 0; i < 0x100; ++i)
            {
                const std::size_t key_index = i % 0x20;
                rolling[key_index] =
                    static_cast<uint8_t>(rolling[key_index] ^ data[0x140 + i]);
                data[0x40 + i] = static_cast<uint8_t>(data[0x40 + i] ^ rolling[key_index]);
            }

            return data;
        }

        std::vector<uint8_t>
        encrypt_usm_video_packet(const std::vector<uint8_t> &packet,
                                 const std::array<uint8_t, 0x40> &video_key)
        {
            std::vector<uint8_t> data = packet;
            if (data.size() < 0x40 + 0x200)
            {
                return data;
            }

            const std::vector<uint8_t> plain = packet;
            std::array<uint8_t, 0x40> rolling = video_key;
            const std::size_t encrypted_size = data.size() - 0x40;

            for (std::size_t i = 0x100; i < encrypted_size; ++i)
            {
                const std::size_t packet_index = 0x40 + i;
                const std::size_t key_index = 0x20 + (i % 0x20);
                const uint8_t plain_byte = plain[packet_index];
                data[packet_index] = static_cast<uint8_t>(plain_byte ^ rolling[key_index]);
                rolling[key_index] =
                    static_cast<uint8_t>(plain_byte ^ video_key[key_index]);
            }

            for (std::size_t i = 0; i < 0x100; ++i)
            {
                const std::size_t key_index = i % 0x20;
                rolling[key_index] =
                    static_cast<uint8_t>(rolling[key_index] ^ plain[0x140 + i]);
                data[0x40 + i] = static_cast<uint8_t>(plain[0x40 + i] ^ rolling[key_index]);
            }

            return data;
        }

        enum class VideoCodec
        {
            kUnknown,
            kVp9Ivf,
            kH264AnnexB,
            kMpegVideo,
        };

        std::size_t find_vp9_ivf_start(const std::vector<uint8_t> &data)
        {
            constexpr std::array<uint8_t, 4> kIvfSig = {
                static_cast<uint8_t>('D'), static_cast<uint8_t>('K'),
                static_cast<uint8_t>('I'), static_cast<uint8_t>('F')};
            constexpr std::array<uint8_t, 4> kVp9Sig = {
                static_cast<uint8_t>('V'), static_cast<uint8_t>('P'),
                static_cast<uint8_t>('9'), static_cast<uint8_t>('0')};

            if (data.size() < 32U)
            {
                return std::string::npos;
            }

            for (std::size_t i = 0; i + 32U <= data.size(); ++i)
            {
                if (!std::equal(kIvfSig.begin(), kIvfSig.end(),
                                data.begin() + static_cast<std::ptrdiff_t>(i)))
                {
                    continue;
                }
                if (!std::equal(kVp9Sig.begin(), kVp9Sig.end(),
                                data.begin() + static_cast<std::ptrdiff_t>(i + 8U)))
                {
                    continue;
                }
                const uint16_t header_size = read_u16_le(data.data() + i + 6U);
                if (header_size >= 32U &&
                    i + static_cast<std::size_t>(header_size) <= data.size())
                {
                    return i;
                }
            }

            return std::string::npos;
        }

        bool is_vp9_ivf_stream(const std::vector<uint8_t> &data)
        {
            return find_vp9_ivf_start(data) != std::string::npos;
        }

        bool is_h264_annexb_stream(const std::vector<uint8_t> &data)
        {
            if (data.size() < 5U)
            {
                return false;
            }

            auto is_h264_nal = [](uint8_t nal_header)
            {
                const uint8_t nal_type = static_cast<uint8_t>(nal_header & 0x1FU);
                return nal_type == 1U || nal_type == 5U || nal_type == 6U ||
                       nal_type == 7U || nal_type == 8U || nal_type == 9U;
            };

            for (std::size_t i = 0; i + 4U < data.size(); ++i)
            {
                if (data[i] != 0U || data[i + 1U] != 0U)
                {
                    continue;
                }

                if (data[i + 2U] == 1U)
                {
                    if (is_h264_nal(data[i + 3U]))
                    {
                        return true;
                    }
                    continue;
                }

                if (data[i + 2U] == 0U && data[i + 3U] == 1U && i + 4U < data.size())
                {
                    if (is_h264_nal(data[i + 4U]))
                    {
                        return true;
                    }
                }
            }
            return false;
        }

        bool is_mpeg_video_stream(const std::vector<uint8_t> &data)
        {
            if (data.size() < 4U)
            {
                return false;
            }
            for (std::size_t i = 0; i + 4U <= data.size(); ++i)
            {
                if (data[i] != 0U || data[i + 1U] != 0U || data[i + 2U] != 1U)
                {
                    continue;
                }
                const uint8_t code = data[i + 3U];
                if (code == 0x00U || code == 0xB3U || code == 0xB8U)
                {
                    return true;
                }
            }
            return false;
        }

        VideoCodec detect_video_codec(const std::vector<uint8_t> &data)
        {
            if (is_vp9_ivf_stream(data))
            {
                return VideoCodec::kVp9Ivf;
            }
            if (is_h264_annexb_stream(data))
            {
                return VideoCodec::kH264AnnexB;
            }
            if (is_mpeg_video_stream(data))
            {
                return VideoCodec::kMpegVideo;
            }
            return VideoCodec::kUnknown;
        }
        struct UsmVideoStream
        {
            std::vector<uint8_t> data;
            VideoCodec codec = VideoCodec::kUnknown;
        };

        bool extract_usm_video_stream(const std::filesystem::path &source,
                                      UsmVideoStream &out)
        {
            std::ifstream in(source, std::ios::binary);
            if (!in)
            {
                return false;
            }

            constexpr uint64_t kUsmKey = 0x7F4551499DF55E68ULL;
            constexpr std::array<uint8_t, 4> kSfv = {
                static_cast<uint8_t>('@'), static_cast<uint8_t>('S'),
                static_cast<uint8_t>('F'), static_cast<uint8_t>('V')};

            struct ChannelData
            {
                bool seen = false;
                VideoCodec codec = VideoCodec::kUnknown;
                std::vector<uint8_t> data;
            };

            std::array<ChannelData, 256> channels{};
            std::vector<uint8_t> channel_order;

            const UsmKeys keys = make_usm_keys(kUsmKey);
            std::array<uint8_t, 0x20> header{};

            while (true)
            {
                in.read(reinterpret_cast<char *>(header.data()),
                        static_cast<std::streamsize>(header.size()));
                const std::streamsize read_bytes = in.gcount();
                if (read_bytes == 0)
                {
                    break;
                }
                if (read_bytes != static_cast<std::streamsize>(header.size()))
                {
                    return false;
                }

                const uint32_t chunk_size_after_header = read_u32_be(header.data() + 4);
                const uint32_t payload_offset = header[9];
                const uint32_t padding_size = read_u16_be(header.data() + 10);
                const uint8_t channel_number = header[12];
                const uint8_t payload_type = static_cast<uint8_t>(header[15] & 0x03U);

                if (chunk_size_after_header < payload_offset + padding_size)
                {
                    return false;
                }

                const uint32_t payload_size =
                    chunk_size_after_header - payload_offset - padding_size;
                const uint32_t extra_offset_bytes =
                    payload_offset > 0x18U ? payload_offset - 0x18U : 0U;
                if (extra_offset_bytes > 0U)
                {
                    in.seekg(static_cast<std::streamoff>(extra_offset_bytes), std::ios::cur);
                    if (!in)
                    {
                        return false;
                    }
                }

                std::vector<uint8_t> payload(payload_size);
                if (!read_exact(in, payload.data(), payload.size()))
                {
                    return false;
                }

                if (padding_size > 0U)
                {
                    in.seekg(static_cast<std::streamoff>(padding_size), std::ios::cur);
                    if (!in)
                    {
                        return false;
                    }
                }

                if (payload_type != 0U || payload.empty())
                {
                    continue;
                }

                if (!std::equal(kSfv.begin(), kSfv.end(), header.begin()))
                {
                    continue;
                }

                std::vector<uint8_t> decoded =
                    decrypt_usm_video_packet(payload, keys.video);
                ChannelData &channel = channels[channel_number];

                if (!channel.seen)
                {
                    channel.seen = true;
                    channel_order.push_back(channel_number);
                }

                channel.data.insert(channel.data.end(), decoded.begin(), decoded.end());
                if (channel.codec == VideoCodec::kUnknown)
                {
                    channel.codec = detect_video_codec(channel.data);
                }
            }

            if (channel_order.empty())
            {
                return false;
            }

            uint8_t selected_channel = channel_order.front();
            for (const uint8_t c : channel_order)
            {
                if (channels[c].codec == VideoCodec::kVp9Ivf)
                {
                    selected_channel = c;
                    break;
                }
            }
            out.data = std::move(channels[selected_channel].data);
            out.codec = detect_video_codec(out.data);
            return !out.data.empty();
        }

        bool fallback_ffmpeg_vp9_to_h264(const std::vector<uint8_t> &vp9_ivf,
                                         const std::filesystem::path &target_mp4)
        {
            if (vp9_ivf.empty())
            {
                return false;
            }

            if (!target_mp4.parent_path().empty())
            {
                std::filesystem::create_directories(target_mp4.parent_path());
            }

            const auto h264_encoders = resolve_ffmpeg_h264_encoders();
            if (h264_encoders.empty())
            {
                return false;
            }

            for (const auto &encoder : h264_encoders)
            {
                remove_file_if_exists(target_mp4);
#if defined(_WIN32)
                std::vector<std::wstring> args = {L"-y", L"-loglevel", L"error"};
                append_hwaccel_arg(args);
                args.insert(args.end(), {L"-f", L"ivf", L"-i", L"pipe:0", L"-an", L"-c:v",
                                         widen_ascii(encoder), L"-pix_fmt", L"yuv420p",
                                         target_mp4.wstring()});
                const bool ok = run_ffmpeg_feed_stdin(args, vp9_ivf);
#else
                std::vector<std::string> args = {"-y", "-loglevel", "error"};
                append_hwaccel_arg(args);
                args.insert(args.end(),
                            {"-f", "ivf", "-i", "pipe:0", "-an", "-c:v", encoder,
                             "-pix_fmt", "yuv420p", path_to_utf8(target_mp4)});
                const bool ok = run_ffmpeg_feed_stdin(args, vp9_ivf);
#endif
                if (ok && file_non_empty(target_mp4))
                {
                    return true;
                }
            }

            return false;
        }

        bool transcode_mp4_to_vp9_ivf_bytes(const std::filesystem::path &source_mp4,
                                            std::vector<uint8_t> &target_ivf)
        {
            if (!file_non_empty(source_mp4))
            {
                return false;
            }

            target_ivf.clear();

            const auto vp9_encoders = resolve_ffmpeg_vp9_encoders();
            if (vp9_encoders.empty())
            {
                return false;
            }

            for (const auto &encoder : vp9_encoders)
            {
                target_ivf.clear();
#if defined(_WIN32)
                std::vector<std::wstring> args = {L"-y", L"-loglevel", L"error"};
                append_hwaccel_arg(args);
                args.insert(args.end(), {L"-i", source_mp4.wstring(), L"-an", L"-c:v",
                                         widen_ascii(encoder), L"-f", L"ivf", L"pipe:1"});
                const bool ok = run_ffmpeg_capture_stdout(args, target_ivf);
#else
                std::vector<std::string> args = {"-y", "-loglevel", "error"};
                append_hwaccel_arg(args);
                args.insert(args.end(), {"-i", path_to_utf8(source_mp4), "-an", "-c:v",
                                         encoder, "-f", "ivf", "pipe:1"});
                const bool ok = run_ffmpeg_capture_stdout(args, target_ivf);
#endif
                if (ok && is_vp9_ivf_stream(target_ivf))
                {
                    return true;
                }
            }

            target_ivf.clear();
            return false;
        }

        bool read_binary_file(const std::filesystem::path &path,
                              std::vector<uint8_t> &out)
        {
            std::ifstream in(path, std::ios::binary);
            if (!in)
            {
                return false;
            }
            in.seekg(0, std::ios::end);
            const auto size = in.tellg();
            if (size <= 0)
            {
                return false;
            }
            in.seekg(0, std::ios::beg);
            out.resize(static_cast<std::size_t>(size));
            in.read(reinterpret_cast<char *>(out.data()),
                    static_cast<std::streamsize>(out.size()));
            return static_cast<std::size_t>(in.gcount()) == out.size();
        }

        bool write_binary_file(const std::filesystem::path &path,
                               const std::vector<uint8_t> &data)
        {
            if (!path.parent_path().empty())
            {
                std::filesystem::create_directories(path.parent_path());
            }
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            if (!out)
            {
                return false;
            }
            out.write(reinterpret_cast<const char *>(data.data()),
                      static_cast<std::streamsize>(data.size()));
            out.flush();
            return out.good() && file_non_empty(path);
        }

        enum class UtfValueKind
        {
            kNull,
            kU64,
            kI64,
            kF64,
            kString,
            kBinary,
        };

        struct UtfValue
        {
            UtfValueKind kind = UtfValueKind::kNull;
            uint64_t u64 = 0;
            int64_t i64 = 0;
            double f64 = 0.0;
            std::string text;
            std::vector<uint8_t> binary;
        };

        struct UtfColumn
        {
            std::string name;
            uint8_t value_type = 0;
            uint8_t storage_mode = 0;
            UtfValue constant_value;
        };

        struct UtfTable
        {
            std::string name;
            std::vector<UtfColumn> columns;
            std::vector<std::vector<UtfValue>> rows;
        };

        constexpr uint8_t kUtfStorageZero = 0x10U;
        constexpr uint8_t kUtfStorageConstant = 0x30U;
        constexpr uint8_t kUtfStorageRow = 0x50U;

        bool utf_load_cstring(const std::vector<uint8_t> &bytes, std::size_t begin,
                              std::size_t end, uint32_t relative_offset,
                              std::string &out)
        {
            if (begin > end || end > bytes.size())
            {
                return false;
            }
            if (relative_offset > end - begin)
            {
                return false;
            }
            const std::size_t pos = begin + static_cast<std::size_t>(relative_offset);
            if (pos >= end)
            {
                out.clear();
                return true;
            }
            std::size_t term = pos;
            while (term < end && bytes[term] != 0U)
            {
                ++term;
            }
            out.assign(reinterpret_cast<const char *>(bytes.data() +
                                                      static_cast<std::ptrdiff_t>(pos)),
                       term - pos);
            return true;
        }

        bool utf_read_value(const std::vector<uint8_t> &bytes, std::size_t read_pos,
                            uint8_t value_type, std::size_t strings_begin,
                            std::size_t strings_end, std::size_t binary_begin,
                            std::size_t binary_end, UtfValue &out,
                            std::size_t &consumed)
        {
            consumed = 0;
            if (read_pos >= bytes.size())
            {
                return false;
            }

            const auto require = [&](std::size_t n) -> bool
            {
                return n <= bytes.size() - read_pos;
            };

            out = UtfValue{};
            switch (value_type)
            {
            case 0x00:
            {
                if (!require(1U))
                {
                    return false;
                }
                out.kind = UtfValueKind::kU64;
                out.u64 = bytes[read_pos];
                consumed = 1U;
                return true;
            }
            case 0x01:
            {
                if (!require(1U))
                {
                    return false;
                }
                out.kind = UtfValueKind::kI64;
                out.i64 = static_cast<int64_t>(
                    read_i8(bytes.data() + static_cast<std::ptrdiff_t>(read_pos)));
                consumed = 1U;
                return true;
            }
            case 0x02:
            {
                if (!require(2U))
                {
                    return false;
                }
                out.kind = UtfValueKind::kU64;
                out.u64 = read_u16_be(bytes.data() + static_cast<std::ptrdiff_t>(read_pos));
                consumed = 2U;
                return true;
            }
            case 0x03:
            {
                if (!require(2U))
                {
                    return false;
                }
                out.kind = UtfValueKind::kI64;
                out.i64 = static_cast<int64_t>(
                    read_i16_be(bytes.data() + static_cast<std::ptrdiff_t>(read_pos)));
                consumed = 2U;
                return true;
            }
            case 0x04:
            {
                if (!require(4U))
                {
                    return false;
                }
                out.kind = UtfValueKind::kU64;
                out.u64 = read_u32_be(bytes.data() + static_cast<std::ptrdiff_t>(read_pos));
                consumed = 4U;
                return true;
            }
            case 0x05:
            {
                if (!require(4U))
                {
                    return false;
                }
                out.kind = UtfValueKind::kI64;
                out.i64 = static_cast<int64_t>(
                    read_i32_be(bytes.data() + static_cast<std::ptrdiff_t>(read_pos)));
                consumed = 4U;
                return true;
            }
            case 0x06:
            {
                if (!require(8U))
                {
                    return false;
                }
                out.kind = UtfValueKind::kU64;
                out.u64 = read_u64_be(bytes.data() + static_cast<std::ptrdiff_t>(read_pos));
                consumed = 8U;
                return true;
            }
            case 0x07:
            {
                if (!require(8U))
                {
                    return false;
                }
                out.kind = UtfValueKind::kI64;
                out.i64 = read_i64_be(bytes.data() + static_cast<std::ptrdiff_t>(read_pos));
                consumed = 8U;
                return true;
            }
            case 0x08:
            {
                if (!require(4U))
                {
                    return false;
                }
                const uint32_t bits =
                    read_u32_be(bytes.data() + static_cast<std::ptrdiff_t>(read_pos));
                float v = 0.0f;
                std::memcpy(&v, &bits, sizeof(v));
                out.kind = UtfValueKind::kF64;
                out.f64 = static_cast<double>(v);
                consumed = 4U;
                return true;
            }
            case 0x09:
            {
                if (!require(8U))
                {
                    return false;
                }
                const uint64_t bits =
                    read_u64_be(bytes.data() + static_cast<std::ptrdiff_t>(read_pos));
                double v = 0.0;
                std::memcpy(&v, &bits, sizeof(v));
                out.kind = UtfValueKind::kF64;
                out.f64 = v;
                consumed = 8U;
                return true;
            }
            case 0x0A:
            {
                if (!require(4U))
                {
                    return false;
                }
                const uint32_t text_offset =
                    read_u32_be(bytes.data() + static_cast<std::ptrdiff_t>(read_pos));
                std::string text;
                if (!utf_load_cstring(bytes, strings_begin, strings_end, text_offset,
                                      text))
                {
                    return false;
                }
                out.kind = UtfValueKind::kString;
                out.text = std::move(text);
                consumed = 4U;
                return true;
            }
            case 0x0B:
            {
                if (!require(8U))
                {
                    return false;
                }
                const uint32_t data_offset =
                    read_u32_be(bytes.data() + static_cast<std::ptrdiff_t>(read_pos));
                const uint32_t data_size =
                    read_u32_be(bytes.data() + static_cast<std::ptrdiff_t>(read_pos + 4U));
                if (binary_begin > binary_end || data_offset > binary_end - binary_begin ||
                    data_size > binary_end - binary_begin - data_offset)
                {
                    return false;
                }
                const std::size_t begin =
                    binary_begin + static_cast<std::size_t>(data_offset);
                const std::size_t end = begin + static_cast<std::size_t>(data_size);
                out.kind = UtfValueKind::kBinary;
                out.binary.assign(bytes.begin() + static_cast<std::ptrdiff_t>(begin),
                                  bytes.begin() + static_cast<std::ptrdiff_t>(end));
                consumed = 8U;
                return true;
            }
            default:
                return false;
            }
        }

        bool parse_utf_table_at(const std::vector<uint8_t> &bytes, std::size_t offset,
                                UtfTable &out, std::size_t *consumed_size = nullptr)
        {
            if (offset > bytes.size() || bytes.size() - offset < 0x20U)
            {
                return false;
            }
            const uint8_t *p = bytes.data() + static_cast<std::ptrdiff_t>(offset);
            if (!(p[0] == static_cast<uint8_t>('@') &&
                  p[1] == static_cast<uint8_t>('U') &&
                  p[2] == static_cast<uint8_t>('T') &&
                  p[3] == static_cast<uint8_t>('F')))
            {
                return false;
            }

            const uint32_t table_payload_size = read_u32_be(p + 4U);
            const std::size_t table_total_size =
                static_cast<std::size_t>(table_payload_size) + 8U;
            if (table_total_size < 0x20U || table_total_size > bytes.size() - offset)
            {
                return false;
            }

            const uint32_t rows_offset_raw = read_u32_be(p + 8U);
            const uint32_t strings_offset = read_u32_be(p + 12U);
            const uint32_t binary_offset = read_u32_be(p + 16U);
            const uint32_t table_name_offset = read_u32_be(p + 20U);
            const uint16_t column_count = read_u16_be(p + 24U);
            const uint16_t row_length = read_u16_be(p + 26U);
            const uint32_t row_count = read_u32_be(p + 28U);

            const std::size_t payload_begin = offset + 8U;
            const std::size_t table_end = offset + table_total_size;

            uint32_t rows_offset = rows_offset_raw;
            if (rows_offset >= table_payload_size)
            {
                rows_offset &= 0xFFFFU;
            }
            if (rows_offset > table_payload_size || strings_offset > table_payload_size ||
                binary_offset > table_payload_size)
            {
                return false;
            }

            const std::size_t rows_begin = payload_begin + rows_offset;
            const std::size_t strings_begin = payload_begin + strings_offset;
            const std::size_t binary_begin = payload_begin + binary_offset;
            if (rows_begin > table_end || strings_begin > table_end ||
                binary_begin > table_end)
            {
                return false;
            }

            std::size_t schema_pos = offset + 0x20U;
            std::vector<UtfColumn> columns;
            columns.reserve(static_cast<std::size_t>(column_count));
            for (uint16_t i = 0; i < column_count; ++i)
            {
                if (schema_pos > table_end || table_end - schema_pos < 5U)
                {
                    return false;
                }

                const uint8_t flags = bytes[schema_pos];
                ++schema_pos;
                const uint8_t storage_mode = static_cast<uint8_t>(flags & 0xF0U);
                const uint8_t value_type = static_cast<uint8_t>(flags & 0x0FU);
                const uint32_t name_offset =
                    read_u32_be(bytes.data() + static_cast<std::ptrdiff_t>(schema_pos));
                schema_pos += 4U;

                std::string name;
                if (!utf_load_cstring(bytes, strings_begin, table_end, name_offset, name))
                {
                    return false;
                }

                UtfColumn column;
                column.name = std::move(name);
                column.value_type = value_type;
                column.storage_mode = storage_mode;

                if (storage_mode == kUtfStorageConstant)
                {
                    std::size_t consumed = 0U;
                    if (!utf_read_value(bytes, schema_pos, value_type, strings_begin,
                                        table_end, binary_begin, table_end,
                                        column.constant_value, consumed))
                    {
                        return false;
                    }
                    if (consumed > table_end - schema_pos)
                    {
                        return false;
                    }
                    schema_pos += consumed;
                }
                else if (storage_mode != kUtfStorageZero &&
                         storage_mode != kUtfStorageRow)
                {
                    return false;
                }

                columns.push_back(std::move(column));
            }

            if (rows_begin > table_end)
            {
                return false;
            }
            const std::size_t total_rows_size = static_cast<std::size_t>(row_length) *
                                                static_cast<std::size_t>(row_count);
            if (total_rows_size > table_end - rows_begin)
            {
                return false;
            }

            std::vector<std::vector<UtfValue>> rows;
            rows.reserve(static_cast<std::size_t>(row_count));
            for (uint32_t r = 0; r < row_count; ++r)
            {
                const std::size_t row_begin =
                    rows_begin + static_cast<std::size_t>(r) * row_length;
                const std::size_t row_end = row_begin + row_length;
                std::size_t row_pos = row_begin;

                std::vector<UtfValue> row_values;
                row_values.reserve(columns.size());
                for (const auto &column : columns)
                {
                    UtfValue value;
                    if (column.storage_mode == kUtfStorageZero)
                    {
                        value.kind = UtfValueKind::kNull;
                    }
                    else if (column.storage_mode == kUtfStorageConstant)
                    {
                        value = column.constant_value;
                    }
                    else
                    { // kUtfStorageRow
                        std::size_t consumed = 0U;
                        if (!utf_read_value(bytes, row_pos, column.value_type, strings_begin,
                                            table_end, binary_begin, table_end, value,
                                            consumed))
                        {
                            return false;
                        }
                        if (consumed > row_end - row_pos)
                        {
                            return false;
                        }
                        row_pos += consumed;
                    }
                    row_values.push_back(std::move(value));
                }
                if (row_pos > row_end)
                {
                    return false;
                }
                rows.push_back(std::move(row_values));
            }

            std::string table_name;
            if (!utf_load_cstring(bytes, strings_begin, table_end, table_name_offset,
                                  table_name))
            {
                return false;
            }

            out = UtfTable{};
            out.name = std::move(table_name);
            out.columns = std::move(columns);
            out.rows = std::move(rows);
            if (consumed_size != nullptr)
            {
                *consumed_size = table_total_size;
            }
            return true;
        }

        const UtfValue *utf_find_cell(const UtfTable &table, std::size_t row_index,
                                      const char *column_name)
        {
            if (column_name == nullptr || row_index >= table.rows.size())
            {
                return nullptr;
            }
            for (std::size_t i = 0; i < table.columns.size(); ++i)
            {
                if (table.columns[i].name == column_name)
                {
                    return i < table.rows[row_index].size() ? &table.rows[row_index][i]
                                                            : nullptr;
                }
            }
            return nullptr;
        }

        std::optional<uint64_t> utf_value_as_u64(const UtfValue *value)
        {
            if (value == nullptr)
            {
                return std::nullopt;
            }
            switch (value->kind)
            {
            case UtfValueKind::kU64:
                return value->u64;
            case UtfValueKind::kI64:
                if (value->i64 < 0)
                {
                    return std::nullopt;
                }
                return static_cast<uint64_t>(value->i64);
            default:
                return std::nullopt;
            }
        }

        std::optional<std::string> utf_value_as_string(const UtfValue *value)
        {
            if (value == nullptr || value->kind != UtfValueKind::kString)
            {
                return std::nullopt;
            }
            return value->text;
        }

        struct AcbMetadata
        {
            std::string stream_awb_name;
            std::vector<uint32_t> stream_awb_ids;
        };

        bool parse_acb_metadata(const std::filesystem::path &acb_path,
                                AcbMetadata &metadata_out)
        {
            metadata_out = AcbMetadata{};
            std::vector<uint8_t> acb_bytes;
            if (!read_binary_file(acb_path, acb_bytes))
            {
                return false;
            }

            UtfTable root;
            if (!parse_utf_table_at(acb_bytes, 0U, root))
            {
                return false;
            }

            std::unordered_map<std::string, UtfTable> tables;
            auto add_table = [&](UtfTable table)
            {
                if (table.name.empty())
                {
                    return;
                }
                tables.emplace(table.name, std::move(table));
            };
            add_table(root);

            std::vector<UtfTable> pending;
            pending.push_back(root);
            for (std::size_t i = 0; i < pending.size(); ++i)
            {
                const auto &table = pending[i];
                for (const auto &row : table.rows)
                {
                    for (const auto &cell : row)
                    {
                        if (cell.kind != UtfValueKind::kBinary || cell.binary.size() < 0x20U)
                        {
                            continue;
                        }
                        UtfTable nested;
                        if (!parse_utf_table_at(cell.binary, 0U, nested))
                        {
                            continue;
                        }
                        if (nested.name.empty() || tables.find(nested.name) != tables.end())
                        {
                            continue;
                        }
                        pending.push_back(nested);
                        add_table(std::move(nested));
                    }
                }
            }

            const auto stream_awb_it = tables.find("StreamAwb");
            if (stream_awb_it != tables.end() && !stream_awb_it->second.rows.empty())
            {
                const auto name =
                    utf_value_as_string(utf_find_cell(stream_awb_it->second, 0U, "Name"));
                if (name.has_value())
                {
                    metadata_out.stream_awb_name = *name;
                }
            }

            const auto find_table =
                [&](const std::vector<const char *> &names) -> const UtfTable *
            {
                for (const char *name : names)
                {
                    const auto it = tables.find(name);
                    if (it != tables.end())
                    {
                        return &it->second;
                    }
                }
                return nullptr;
            };

            const UtfTable *cue_table = find_table({"CueTable", "Cue"});
            const UtfTable *synth_table = find_table({"SynthTable", "Synth"});
            const UtfTable *sequence_table = find_table({"SequenceTable", "Sequence"});
            const UtfTable *track_table = find_table({"TrackTable", "Track"});
            const UtfTable *track_event_table =
                find_table({"TrackEvent", "TrackEventTable"});
            const UtfTable *waveform_table = find_table({"WaveformTable", "Waveform"});

            const auto parse_be_u16_array =
                [](const UtfValue *value) -> std::vector<uint16_t>
            {
                std::vector<uint16_t> out;
                if (value == nullptr || value->kind != UtfValueKind::kBinary)
                {
                    return out;
                }

                const auto &bytes = value->binary;
                if (bytes.empty())
                {
                    return out;
                }

                out.reserve(bytes.size() / 2U);
                for (std::size_t pos = 0; pos + 1U < bytes.size(); pos += 2U)
                {
                    out.push_back(
                        read_u16_be(bytes.data() + static_cast<std::ptrdiff_t>(pos)));
                }
                return out;
            };

            const auto resolve_waveform_awb_id =
                [&](uint32_t waveform_index,
                    bool streaming_only) -> std::optional<uint32_t>
            {
                if (waveform_table == nullptr ||
                    waveform_index >= waveform_table->rows.size())
                {
                    return std::nullopt;
                }

                const auto streaming_opt = utf_value_as_u64(
                    utf_find_cell(*waveform_table, waveform_index, "Streaming"));
                const bool has_streaming = streaming_opt.has_value();
                const bool is_streaming =
                    !has_streaming || (*streaming_opt != static_cast<uint64_t>(0U));
                if (streaming_only && !is_streaming)
                {
                    return std::nullopt;
                }

                const auto stream_awb_id_opt = utf_value_as_u64(
                    utf_find_cell(*waveform_table, waveform_index, "StreamAwbId"));
                const auto memory_awb_id_opt = utf_value_as_u64(
                    utf_find_cell(*waveform_table, waveform_index, "MemoryAwbId"));
                std::optional<uint64_t> awb_id_opt;
                if (stream_awb_id_opt.has_value() || memory_awb_id_opt.has_value())
                {
                    if (!is_streaming && memory_awb_id_opt.has_value())
                    {
                        awb_id_opt = memory_awb_id_opt;
                    }
                    else if (stream_awb_id_opt.has_value())
                    {
                        awb_id_opt = stream_awb_id_opt;
                    }
                    else
                    {
                        awb_id_opt = memory_awb_id_opt;
                    }
                }
                else
                {
                    // Older ACB variants may only expose the AWB entry id as "Id".
                    awb_id_opt = utf_value_as_u64(
                        utf_find_cell(*waveform_table, waveform_index, "Id"));
                }
                if (!awb_id_opt.has_value() ||
                    *awb_id_opt > static_cast<uint64_t>(UINT32_MAX))
                {
                    return std::nullopt;
                }
                const uint32_t awb_id = static_cast<uint32_t>(*awb_id_opt);
                if (awb_id == 0xFFFFFFFFU || awb_id == 0x0000FFFFU)
                {
                    return std::nullopt;
                }

                return awb_id;
            };

            constexpr uint16_t kRefWaveform = 1U;
            constexpr uint16_t kRefSynth = 2U;
            constexpr uint16_t kRefSequence = 3U;
            constexpr uint16_t kRefNullIndex = 0xFFFFU;
            constexpr uint16_t kCmdReferenceItem = 0x07D0U;
            constexpr uint16_t kCmdReferenceItem2 = 0x07D3U;

            std::vector<uint32_t> cue0_awb_ids;
            std::set<uint32_t> cue0_unique_ids;
            std::set<uint32_t> visited_synth;
            std::set<uint32_t> visited_sequence;
            std::set<uint32_t> visited_track;
            std::set<uint32_t> visited_track_event;

            std::function<void(uint16_t, uint16_t)> resolve_reference;
            std::function<void(uint16_t)> resolve_synth;
            std::function<void(uint16_t)> resolve_sequence;
            std::function<void(uint16_t)> resolve_track;
            std::function<void(uint16_t)> resolve_track_event;

            resolve_reference = [&](uint16_t ref_type, uint16_t ref_index)
            {
                if (ref_index == kRefNullIndex)
                {
                    return;
                }

                switch (ref_type)
                {
                case kRefWaveform:
                {
                    const auto awb_id =
                        resolve_waveform_awb_id(static_cast<uint32_t>(ref_index), false);
                    if (awb_id.has_value() && cue0_unique_ids.insert(*awb_id).second)
                    {
                        cue0_awb_ids.push_back(*awb_id);
                    }
                    break;
                }
                case kRefSynth:
                    resolve_synth(ref_index);
                    break;
                case kRefSequence:
                    resolve_sequence(ref_index);
                    break;
                default:
                    break;
                }
            };

            resolve_synth = [&](uint16_t synth_index)
            {
                if (synth_table == nullptr || synth_index >= synth_table->rows.size())
                {
                    return;
                }
                if (!visited_synth.insert(synth_index).second)
                {
                    return;
                }

                const auto ref_items = parse_be_u16_array(
                    utf_find_cell(*synth_table, synth_index, "ReferenceItems"));
                for (std::size_t i = 0; i + 1U < ref_items.size(); i += 2U)
                {
                    resolve_reference(ref_items[i], ref_items[i + 1U]);
                }
            };

            resolve_track_event = [&](uint16_t track_event_index)
            {
                if (track_event_table == nullptr ||
                    track_event_index >= track_event_table->rows.size())
                {
                    return;
                }
                if (!visited_track_event.insert(track_event_index).second)
                {
                    return;
                }

                const UtfValue *command_value =
                    utf_find_cell(*track_event_table, track_event_index, "Command");
                if (command_value == nullptr ||
                    command_value->kind != UtfValueKind::kBinary)
                {
                    return;
                }

                const auto &commands = command_value->binary;
                for (std::size_t pos = 0; pos + 3U <= commands.size();)
                {
                    const uint16_t command_type =
                        read_u16_be(commands.data() + static_cast<std::ptrdiff_t>(pos));
                    const uint8_t param_size = commands[pos + 2U];
                    pos += 3U;

                    if (pos + static_cast<std::size_t>(param_size) > commands.size())
                    {
                        break;
                    }

                    if ((command_type == kCmdReferenceItem ||
                         command_type == kCmdReferenceItem2) &&
                        param_size >= 4U)
                    {
                        const uint16_t ref_type =
                            read_u16_be(commands.data() + static_cast<std::ptrdiff_t>(pos));
                        const uint16_t ref_index = read_u16_be(
                            commands.data() + static_cast<std::ptrdiff_t>(pos + 2U));
                        resolve_reference(ref_type, ref_index);
                    }

                    pos += static_cast<std::size_t>(param_size);
                }
            };

            resolve_track = [&](uint16_t track_index)
            {
                if (track_table == nullptr || track_index >= track_table->rows.size())
                {
                    return;
                }
                if (!visited_track.insert(track_index).second)
                {
                    return;
                }

                const auto event_index_opt = utf_value_as_u64(
                    utf_find_cell(*track_table, track_index, "EventIndex"));
                if (!event_index_opt.has_value() ||
                    *event_index_opt > static_cast<uint64_t>(UINT16_MAX) ||
                    *event_index_opt == static_cast<uint64_t>(kRefNullIndex))
                {
                    return;
                }

                resolve_track_event(static_cast<uint16_t>(*event_index_opt));
            };

            resolve_sequence = [&](uint16_t sequence_index)
            {
                if (sequence_table == nullptr ||
                    sequence_index >= sequence_table->rows.size())
                {
                    return;
                }
                if (!visited_sequence.insert(sequence_index).second)
                {
                    return;
                }

                const auto track_indices = parse_be_u16_array(
                    utf_find_cell(*sequence_table, sequence_index, "TrackIndex"));
                if (track_indices.empty())
                {
                    return;
                }

                std::size_t track_count = track_indices.size();
                const auto num_tracks_opt = utf_value_as_u64(
                    utf_find_cell(*sequence_table, sequence_index, "NumTracks"));
                if (num_tracks_opt.has_value() &&
                    *num_tracks_opt < static_cast<uint64_t>(track_count))
                {
                    track_count = static_cast<std::size_t>(*num_tracks_opt);
                }

                for (std::size_t i = 0; i < track_count; ++i)
                {
                    resolve_track(track_indices[i]);
                }
            };

            if (cue_table != nullptr && !cue_table->rows.empty())
            {
                const auto cue_ref_type_opt =
                    utf_value_as_u64(utf_find_cell(*cue_table, 0U, "ReferenceType"));
                const auto cue_ref_index_opt =
                    utf_value_as_u64(utf_find_cell(*cue_table, 0U, "ReferenceIndex"));
                if (cue_ref_type_opt.has_value() && cue_ref_index_opt.has_value() &&
                    *cue_ref_type_opt <= static_cast<uint64_t>(UINT16_MAX) &&
                    *cue_ref_index_opt <= static_cast<uint64_t>(UINT16_MAX))
                {
                    resolve_reference(static_cast<uint16_t>(*cue_ref_type_opt),
                                      static_cast<uint16_t>(*cue_ref_index_opt));
                }
            }

            if (!cue0_awb_ids.empty())
            {
                metadata_out.stream_awb_ids = std::move(cue0_awb_ids);
                return true;
            }

            // Fallback for ACB variants where cue traversal is not available.
            if (waveform_table != nullptr)
            {
                std::set<uint32_t> unique_ids;
                for (std::size_t row = 0; row < waveform_table->rows.size(); ++row)
                {
                    const auto awb_id =
                        resolve_waveform_awb_id(static_cast<uint32_t>(row), true);
                    if (!awb_id.has_value())
                    {
                        continue;
                    }
                    if (unique_ids.insert(*awb_id).second)
                    {
                        metadata_out.stream_awb_ids.push_back(*awb_id);
                    }
                }
            }

            return true;
        }

        bool build_minimal_dat_from_vp9_ivf(const std::vector<uint8_t> &vp9_ivf,
                                            const std::filesystem::path &target_dat)
        {
            if (!is_vp9_ivf_stream(vp9_ivf))
            {
                return false;
            }

            constexpr uint64_t kUsmKey = 0x7F4551499DF55E68ULL;
            const UsmKeys keys = make_usm_keys(kUsmKey);
            constexpr std::size_t kPayloadChunkSize = 0x8000U;

            std::vector<uint8_t> out;
            out.reserve(vp9_ivf.size() +
                        (vp9_ivf.size() / kPayloadChunkSize + 1U) * 0x20U);

            std::size_t cursor = 0;
            while (cursor < vp9_ivf.size())
            {
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
                if (encrypted.size() != payload_size)
                {
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

            return write_binary_file(target_dat, out);
        }

        bool transcode_vp9_ivf_to_h264_mp4(const std::vector<uint8_t> &vp9_ivf,
                                           const std::filesystem::path &target_mp4)
        {
            return fallback_ffmpeg_vp9_to_h264(vp9_ivf, target_mp4);
        }

        std::string usm_stream_extension(VideoCodec codec)
        {
            switch (codec)
            {
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
                                           const std::filesystem::path &target_mp4)
        {
            if (!file_non_empty(stream_file))
            {
                return false;
            }
            if (!target_mp4.parent_path().empty())
            {
                std::filesystem::create_directories(target_mp4.parent_path());
            }

#if defined(_WIN32)
            std::vector<std::wstring> args = {L"-y", L"-loglevel", L"error"};
            args.insert(args.end(), {L"-i", stream_file.wstring(), L"-an", L"-c:v",
                                     L"copy", target_mp4.wstring()});
            const bool ok = run_ffmpeg_process(args);
#else
            std::vector<std::string> args = {"-y", "-loglevel", "error"};
            args.insert(args.end(), {"-i", path_to_utf8(stream_file), "-an", "-c:v",
                                     "copy", path_to_utf8(target_mp4)});
            const bool ok = run_ffmpeg_process(args);
#endif
            return ok && file_non_empty(target_mp4);
        }

        bool transcode_extracted_stream_to_h264_mp4(
            const std::filesystem::path &stream_file,
            const std::filesystem::path &target_mp4)
        {
            if (!file_non_empty(stream_file))
            {
                return false;
            }
            if (!target_mp4.parent_path().empty())
            {
                std::filesystem::create_directories(target_mp4.parent_path());
            }

            const auto h264_encoders = resolve_ffmpeg_h264_encoders();
            if (h264_encoders.empty())
            {
                return false;
            }

            for (const auto &encoder : h264_encoders)
            {
                remove_file_if_exists(target_mp4);
#if defined(_WIN32)
                std::vector<std::wstring> args = {L"-y", L"-loglevel", L"error"};
                append_hwaccel_arg(args);
                args.insert(args.end(), {L"-i", stream_file.wstring(), L"-an", L"-c:v",
                                         widen_ascii(encoder), L"-pix_fmt", L"yuv420p",
                                         target_mp4.wstring()});
                const bool ok = run_ffmpeg_process(args);
#else
                std::vector<std::string> args = {"-y", "-loglevel", "error"};
                append_hwaccel_arg(args);
                args.insert(args.end(),
                            {"-i", path_to_utf8(stream_file), "-an", "-c:v", encoder,
                             "-pix_fmt", "yuv420p", path_to_utf8(target_mp4)});
                const bool ok = run_ffmpeg_process(args);
#endif
                if (ok && file_non_empty(target_mp4))
                {
                    return true;
                }
            }

            return false;
        }

        bool convert_usm_to_mp4(const std::filesystem::path &source,
                                const std::filesystem::path &target_mp4)
        {
            UsmVideoStream stream;
            if (!extract_usm_video_stream(source, stream))
            {
                return false;
            }

            if (stream.codec == VideoCodec::kVp9Ivf)
            {
                return transcode_vp9_ivf_to_h264_mp4(stream.data, target_mp4);
            }

            const auto tmp_dir = make_temp_work_dir();
            const auto stream_file =
                tmp_dir / ("video_stream" + usm_stream_extension(stream.codec));
            if (!write_binary_file(stream_file, stream.data))
            {
                return false;
            }

            remove_file_if_exists(target_mp4);
            if (remux_extracted_stream_to_mp4(stream_file, target_mp4))
            {
                return true;
            }

            remove_file_if_exists(target_mp4);
            return transcode_extracted_stream_to_h264_mp4(stream_file, target_mp4);
        }

        struct Afs2Entry
        {
            uint32_t id = 0U;
            std::size_t begin = 0;
            std::size_t end = 0;
        };

        bool parse_afs2_entries(const std::vector<uint8_t> &awb_bytes,
                                std::vector<Afs2Entry> &out_entries,
                                uint16_t *subkey_out = nullptr)
        {
            out_entries.clear();
            if (awb_bytes.size() < 16U)
            {
                return false;
            }
            if (!(awb_bytes[0] == static_cast<uint8_t>('A') &&
                  awb_bytes[1] == static_cast<uint8_t>('F') &&
                  awb_bytes[2] == static_cast<uint8_t>('S') &&
                  awb_bytes[3] == static_cast<uint8_t>('2')))
            {
                return false;
            }

            // AFS2 variants seen in the wild:
            // 1) alignment as uint32 at 0x0C.
            // 2) legacy layout: alignment as uint16 at 0x0C + HCA subkey at 0x0E.
            // Parse both and choose the one that yields more recognizable entries.
            const uint8_t pointer_size = awb_bytes[5];
            const uint8_t id_size = awb_bytes[6];
            if (!(id_size == 1U || id_size == 2U || id_size == 4U) ||
                !(pointer_size == 2U || pointer_size == 4U || pointer_size == 8U))
            {
                return false;
            }
            if (subkey_out != nullptr)
            {
                *subkey_out = 0U;
            }

            const uint32_t entry_count_u32 = read_u32_le(awb_bytes.data() + 8);
            if (entry_count_u32 == 0U)
            {
                return false;
            }
            const std::size_t entry_count = static_cast<std::size_t>(entry_count_u32);
            const uint32_t alignment_u32 = read_u32_le(awb_bytes.data() + 12);
            const uint16_t alignment_u16 = read_u16_le(awb_bytes.data() + 12);
            const uint16_t header_subkey = read_u16_le(awb_bytes.data() + 14);

            const std::size_t id_size_bytes = static_cast<std::size_t>(id_size);
            if (entry_count > (SIZE_MAX / id_size_bytes))
            {
                return false;
            }
            const std::size_t id_table_size = entry_count * id_size_bytes;
            const std::size_t id_table_begin = 16U;
            const std::size_t pointer_table_begin = id_table_begin + id_table_size;

            const std::size_t pointer_count = entry_count + 1U;
            if (pointer_count > (SIZE_MAX / static_cast<std::size_t>(pointer_size)))
            {
                return false;
            }
            const std::size_t pointer_table_size =
                pointer_count * static_cast<std::size_t>(pointer_size);
            if (pointer_table_begin > awb_bytes.size() ||
                pointer_table_size > awb_bytes.size() - pointer_table_begin)
            {
                return false;
            }

            std::vector<uint32_t> ids;
            ids.reserve(entry_count);
            for (std::size_t i = 0; i < entry_count; ++i)
            {
                const std::size_t pos = id_table_begin + i * id_size_bytes;
                const uint8_t *p = awb_bytes.data() + static_cast<std::ptrdiff_t>(pos);
                uint32_t id = 0U;
                if (id_size == 1U)
                {
                    id = static_cast<uint32_t>(p[0]);
                }
                else if (id_size == 2U)
                {
                    id = static_cast<uint32_t>(read_u16_le(p));
                }
                else
                {
                    id = read_u32_le(p);
                }
                ids.push_back(id);
            }

            std::vector<uint64_t> pointers;
            pointers.reserve(pointer_count);
            for (std::size_t i = 0; i < pointer_count; ++i)
            {
                const std::size_t pos =
                    pointer_table_begin + i * static_cast<std::size_t>(pointer_size);
                const uint8_t *p = awb_bytes.data() + static_cast<std::ptrdiff_t>(pos);
                uint64_t value = 0;
                if (pointer_size == 2U)
                {
                    value = static_cast<uint64_t>(read_u16_le(p));
                }
                else if (pointer_size == 4U)
                {
                    value = static_cast<uint64_t>(read_u32_le(p));
                }
                else
                {
                    value = read_u64_le(p);
                }
                pointers.push_back(value);
            }

            const uint64_t file_size_u64 = static_cast<uint64_t>(awb_bytes.size());
            const auto looks_like_audio_entry = [&](std::size_t begin) -> bool
            {
                if (begin >= awb_bytes.size())
                {
                    return false;
                }
                const auto normalize_hca_char = [](uint8_t value) -> char
                {
                    return static_cast<char>(value & 0x7FU);
                };
                const std::size_t remain = awb_bytes.size() - begin;
                const bool is_hca = remain >= 4U &&
                                    normalize_hca_char(awb_bytes[begin]) == 'H' &&
                                    normalize_hca_char(awb_bytes[begin + 1U]) == 'C' &&
                                    normalize_hca_char(awb_bytes[begin + 2U]) == 'A' &&
                                    awb_bytes[begin + 3U] == 0x00U;
                const bool is_ehca = remain >= 4U &&
                                     normalize_hca_char(awb_bytes[begin]) == 'E' &&
                                     normalize_hca_char(awb_bytes[begin + 1U]) == 'H' &&
                                     normalize_hca_char(awb_bytes[begin + 2U]) == 'C' &&
                                     normalize_hca_char(awb_bytes[begin + 3U]) == 'A';
                if (is_hca || is_ehca)
                {
                    return true;
                }
                if (remain >= 7U && awb_bytes[begin] == 0x80U &&
                    awb_bytes[begin + 1U] == 0x00U &&
                    awb_bytes[begin + 4U] == static_cast<uint8_t>('A') &&
                    awb_bytes[begin + 5U] == static_cast<uint8_t>('D') &&
                    awb_bytes[begin + 6U] == static_cast<uint8_t>('X'))
                {
                    return true;
                }
                return false;
            };

            struct Afs2ParseCandidate
            {
                std::vector<Afs2Entry> entries;
                uint16_t subkey = 0U;
                std::size_t recognized_count = 0U;
            };
            const auto build_candidate =
                [&](uint64_t alignment, uint16_t subkey,
                    bool align_end_for_non_last) -> Afs2ParseCandidate
            {
                Afs2ParseCandidate candidate;
                candidate.subkey = subkey;
                const uint64_t effective_alignment = alignment == 0U ? 1U : alignment;

                const auto align_up = [&](uint64_t value) -> uint64_t
                {
                    if (effective_alignment <= 1U)
                    {
                        return value;
                    }
                    const uint64_t rem = value % effective_alignment;
                    return rem == 0U ? value : (value + (effective_alignment - rem));
                };

                for (std::size_t i = 0; i < entry_count; ++i)
                {
                    const uint64_t begin = align_up(pointers[i]);
                    uint64_t end = pointers[i + 1U];
                    if (align_end_for_non_last && i + 1U < entry_count)
                    {
                        end = align_up(end);
                    }
                    if (end <= begin || end > file_size_u64)
                    {
                        continue;
                    }
                    const std::size_t begin_pos = static_cast<std::size_t>(begin);
                    if (looks_like_audio_entry(begin_pos))
                    {
                        candidate.recognized_count += 1U;
                    }
                    candidate.entries.push_back(
                        {ids[i], begin_pos, static_cast<std::size_t>(end)});
                }
                return candidate;
            };

            std::vector<Afs2ParseCandidate> candidates;
            candidates.reserve(4);
            candidates.push_back(
                build_candidate(static_cast<uint64_t>(alignment_u32), 0U, false));
            candidates.push_back(
                build_candidate(static_cast<uint64_t>(alignment_u32), 0U, true));
            if (alignment_u16 > 0U &&
                (header_subkey != 0U || alignment_u32 > 0xFFFFU ||
                 alignment_u16 != static_cast<uint16_t>(alignment_u32)))
            {
                candidates.push_back(build_candidate(static_cast<uint64_t>(alignment_u16),
                                                     header_subkey, false));
                candidates.push_back(build_candidate(static_cast<uint64_t>(alignment_u16),
                                                     header_subkey, true));
            }

            const auto better_than = [](const Afs2ParseCandidate &lhs,
                                        const Afs2ParseCandidate &rhs)
            {
                if (lhs.recognized_count != rhs.recognized_count)
                {
                    return lhs.recognized_count > rhs.recognized_count;
                }
                if (lhs.entries.size() != rhs.entries.size())
                {
                    return lhs.entries.size() > rhs.entries.size();
                }
                // Keep non-zero subkey candidate when parse quality ties.
                if (lhs.subkey != rhs.subkey)
                {
                    return lhs.subkey != 0U;
                }
                return false;
            };

            const Afs2ParseCandidate *best = nullptr;
            for (const auto &candidate : candidates)
            {
                if (best == nullptr || better_than(candidate, *best))
                {
                    best = &candidate;
                }
            }
            if (best == nullptr || best->entries.empty())
            {
                return false;
            }

            out_entries = best->entries;
            if (subkey_out != nullptr)
            {
                *subkey_out = best->subkey;
            }
            return true;
        }

        bool transcode_audio_file_to_mp3_ffmpeg(const std::filesystem::path &source,
                                                const std::filesystem::path &target_mp3,
                                                const std::string &encoder)
        {
#if defined(_WIN32)
            std::vector<std::wstring> args = {L"-y", L"-loglevel", L"error"};
            append_audio_hwaccel_arg(args);
            args.insert(args.end(), {L"-i", source.wstring(), L"-vn", L"-c:a",
                                     widen_ascii(encoder), target_mp3.wstring()});
            const bool ok = run_ffmpeg_process(args);
#else
            std::vector<std::string> args = {"-y", "-loglevel", "error"};
            append_audio_hwaccel_arg(args);
            args.insert(args.end(), {"-i", path_to_utf8(source), "-vn", "-c:a", encoder,
                                     path_to_utf8(target_mp3)});
            const bool ok = run_ffmpeg_process(args);
#endif
            return ok && file_non_empty(target_mp3);
        }

        struct HcaDecodeOptions
        {
            bool enabled = false;
            uint32_t low_key = 0U;
            uint32_t high_key = 0U;
            uint16_t sub_key = 0U;
        };

        HcaDecodeOptions make_default_hca_decode_options(uint16_t sub_key)
        {
            // Match MaiChartManager's HCA key for game audio export.
            constexpr uint64_t kMaiHcaKey = 0x7F4551499DF55E68ULL;
            HcaDecodeOptions out;
            out.enabled = true;
            out.low_key = static_cast<uint32_t>(kMaiHcaKey & 0xFFFFFFFFULL);
            out.high_key = static_cast<uint32_t>((kMaiHcaKey >> 32U) & 0xFFFFFFFFULL);
            out.sub_key = sub_key;
            return out;
        }

        bool transcode_audio_stdin_to_mp3_ffmpeg(
            const std::vector<uint8_t> &payload,
            const std::filesystem::path &target_mp3, const std::string &encoder,
            const std::optional<std::string> &force_format,
            const HcaDecodeOptions *hca_decode_options = nullptr)
        {
            if (payload.empty())
            {
                return false;
            }

#if defined(_WIN32)
            std::vector<std::wstring> args = {L"-y", L"-loglevel", L"error"};
            append_audio_hwaccel_arg(args);
            if (force_format.has_value())
            {
                args.push_back(L"-f");
                args.push_back(widen_ascii(*force_format));
                if (*force_format == "hca" && hca_decode_options != nullptr &&
                    hca_decode_options->enabled)
                {
                    args.push_back(L"-hca_lowkey");
                    args.push_back(std::to_wstring(hca_decode_options->low_key));
                    args.push_back(L"-hca_highkey");
                    args.push_back(std::to_wstring(hca_decode_options->high_key));
                    args.push_back(L"-hca_subkey");
                    args.push_back(std::to_wstring(hca_decode_options->sub_key));
                }
            }
            args.insert(args.end(), {L"-i", L"pipe:0", L"-vn", L"-c:a",
                                     widen_ascii(encoder), target_mp3.wstring()});
            const bool ok = run_ffmpeg_feed_stdin(args, payload);
#else
            std::vector<std::string> args = {"-y", "-loglevel", "error"};
            append_audio_hwaccel_arg(args);
            if (force_format.has_value())
            {
                args.push_back("-f");
                args.push_back(*force_format);
                if (*force_format == "hca" && hca_decode_options != nullptr &&
                    hca_decode_options->enabled)
                {
                    args.push_back("-hca_lowkey");
                    args.push_back(std::to_string(hca_decode_options->low_key));
                    args.push_back("-hca_highkey");
                    args.push_back(std::to_string(hca_decode_options->high_key));
                    args.push_back("-hca_subkey");
                    args.push_back(std::to_string(hca_decode_options->sub_key));
                }
            }
            args.insert(args.end(), {"-i", "pipe:0", "-vn", "-c:a", encoder,
                                     path_to_utf8(target_mp3)});
            const bool ok = run_ffmpeg_feed_stdin(args, payload);
#endif
            return ok && file_non_empty(target_mp3);
        }

        enum class AudioPayloadFormat
        {
            kUnknown = 0,
            kHca,
            kAdx,
        };

        char normalize_hca_chunk_char(uint8_t value)
        {
            return static_cast<char>(value & 0x7FU);
        }

        bool is_hca_or_ehca_signature_at(const std::vector<uint8_t> &bytes,
                                         std::size_t begin,
                                         bool *is_ehca_out = nullptr)
        {
            if (begin + 4U > bytes.size())
            {
                return false;
            }
            const bool is_hca = normalize_hca_chunk_char(bytes[begin]) == 'H' &&
                                normalize_hca_chunk_char(bytes[begin + 1U]) == 'C' &&
                                normalize_hca_chunk_char(bytes[begin + 2U]) == 'A' &&
                                bytes[begin + 3U] == 0x00U;
            const bool is_ehca = normalize_hca_chunk_char(bytes[begin]) == 'E' &&
                                 normalize_hca_chunk_char(bytes[begin + 1U]) == 'H' &&
                                 normalize_hca_chunk_char(bytes[begin + 2U]) == 'C' &&
                                 normalize_hca_chunk_char(bytes[begin + 3U]) == 'A';
            if (is_ehca_out != nullptr)
            {
                *is_ehca_out = is_ehca;
            }
            return is_hca || is_ehca;
        }

        AudioPayloadFormat
        detect_audio_payload_format_at(const std::vector<uint8_t> &bytes,
                                       std::size_t begin);

        AudioPayloadFormat
        detect_audio_payload_format(const std::vector<uint8_t> &payload)
        {
            return detect_audio_payload_format_at(payload, 0U);
        }

        std::optional<uint16_t>
        detect_hca_encryption_type(const std::vector<uint8_t> &payload)
        {
            if (payload.size() < 8U)
            {
                return std::nullopt;
            }

            bool is_ehca = false;
            if (!is_hca_or_ehca_signature_at(payload, 0U, &is_ehca))
            {
                return std::nullopt;
            }

            const uint16_t header_size = read_u16_be(payload.data() + 6);
            if (header_size < 8U ||
                static_cast<std::size_t>(header_size) > payload.size())
            {
                return std::nullopt;
            }

            const std::size_t limit = static_cast<std::size_t>(header_size);
            for (std::size_t pos = 8U; pos + 6U <= limit; ++pos)
            {
                if (normalize_hca_chunk_char(payload[pos]) != 'c' ||
                    normalize_hca_chunk_char(payload[pos + 1U]) != 'i' ||
                    normalize_hca_chunk_char(payload[pos + 2U]) != 'p' ||
                    normalize_hca_chunk_char(payload[pos + 3U]) != 'h')
                {
                    continue;
                }
                return read_u16_be(payload.data() + static_cast<std::ptrdiff_t>(pos + 4U));
            }

            // For classic HCA, missing "ciph" means encryption type 0.
            // For EHCA, treat missing "ciph" as unknown and keep keyed fallback enabled.
            if (is_ehca)
            {
                return std::nullopt;
            }
            return static_cast<uint16_t>(0U);
        }

        std::vector<std::optional<std::string>>
        build_awb_entry_demuxer_candidates(const std::vector<uint8_t> &payload)
        {
            std::vector<std::optional<std::string>> candidates;
            switch (detect_audio_payload_format(payload))
            {
            case AudioPayloadFormat::kHca:
                candidates.emplace_back(std::string("hca"));
                break;
            case AudioPayloadFormat::kAdx:
                candidates.emplace_back(std::string("adx"));
                break;
            case AudioPayloadFormat::kUnknown:
                break;
            }

            return candidates;
        }

        AudioPayloadFormat
        detect_audio_payload_format_at(const std::vector<uint8_t> &bytes,
                                       std::size_t begin)
        {
            if (begin >= bytes.size())
            {
                return AudioPayloadFormat::kUnknown;
            }

            const std::size_t remain = bytes.size() - begin;
            if (remain >= 4U && is_hca_or_ehca_signature_at(bytes, begin))
            {
                return AudioPayloadFormat::kHca;
            }

            if (remain >= 7U && bytes[begin] == static_cast<uint8_t>(0x80U) &&
                bytes[begin + 1U] == static_cast<uint8_t>(0x00U) &&
                bytes[begin + 4U] == static_cast<uint8_t>('A') &&
                bytes[begin + 5U] == static_cast<uint8_t>('D') &&
                bytes[begin + 6U] == static_cast<uint8_t>('X'))
            {
                return AudioPayloadFormat::kAdx;
            }

            return AudioPayloadFormat::kUnknown;
        }

        bool scan_embedded_audio_entries(const std::vector<uint8_t> &awb_bytes,
                                         std::vector<Afs2Entry> &out_entries)
        {
            out_entries.clear();
            if (awb_bytes.size() < 8U)
            {
                return false;
            }

            std::vector<std::size_t> starts;
            starts.reserve(16);
            for (std::size_t pos = 0; pos + 4U <= awb_bytes.size(); ++pos)
            {
                const AudioPayloadFormat format =
                    detect_audio_payload_format_at(awb_bytes, pos);
                if (format == AudioPayloadFormat::kUnknown)
                {
                    continue;
                }
                if (!starts.empty() && pos <= starts.back() + 4U)
                {
                    continue;
                }
                starts.push_back(pos);
            }
            if (starts.empty())
            {
                return false;
            }

            for (std::size_t i = 0; i < starts.size(); ++i)
            {
                const std::size_t begin = starts[i];
                const std::size_t end =
                    (i + 1U < starts.size()) ? starts[i + 1U] : awb_bytes.size();
                if (end <= begin)
                {
                    continue;
                }
                out_entries.push_back({static_cast<uint32_t>(i),
                                       static_cast<std::size_t>(begin),
                                       static_cast<std::size_t>(end)});
            }
            if (out_entries.empty())
            {
                return false;
            }

            std::stable_sort(out_entries.begin(), out_entries.end(),
                             [](const Afs2Entry &lhs, const Afs2Entry &rhs)
                             {
                                 return (lhs.end - lhs.begin) > (rhs.end - rhs.begin);
                             });
            constexpr std::size_t kMaxScanEntries = 64U;
            if (out_entries.size() > kMaxScanEntries)
            {
                out_entries.resize(kMaxScanEntries);
            }
            return true;
        }

        bool transcode_awb_afs2_to_mp3_ffmpeg(
            const std::filesystem::path &source_awb,
            const std::filesystem::path &target_mp3,
            const std::vector<std::string> &mp3_encoders,
            const std::vector<uint32_t> *preferred_entry_ids)
        {
            if (mp3_encoders.empty())
            {
                return false;
            }

            std::vector<uint8_t> awb_bytes;
            if (!read_binary_file(source_awb, awb_bytes))
            {
                return false;
            }

            uint16_t awb_subkey = 0U;
            std::vector<Afs2Entry> entries;
            bool parsed_afs2 = parse_afs2_entries(awb_bytes, entries, &awb_subkey);
            if (!parsed_afs2 && entries.empty() &&
                !scan_embedded_audio_entries(awb_bytes, entries))
            {
                return false;
            }
            std::vector<HcaDecodeOptions> hca_decode_options_candidates;
            hca_decode_options_candidates.push_back(
                make_default_hca_decode_options(awb_subkey));
            if (awb_subkey != 0U)
            {
                hca_decode_options_candidates.push_back(
                    make_default_hca_decode_options(0U));
            }

            std::vector<Afs2Entry> candidates;
            candidates.reserve(entries.size());

            std::vector<bool> used(entries.size(), false);
            if (parsed_afs2 && preferred_entry_ids != nullptr &&
                !preferred_entry_ids->empty())
            {
                for (const uint32_t wanted_id : *preferred_entry_ids)
                {
                    for (std::size_t i = 0; i < entries.size(); ++i)
                    {
                        if (!used[i] && entries[i].id == wanted_id)
                        {
                            candidates.push_back(entries[i]);
                            used[i] = true;
                        }
                    }
                }
            }
            const bool has_preferred_candidates = !candidates.empty();

            std::vector<Afs2Entry> remaining;
            remaining.reserve(entries.size());
            for (std::size_t i = 0; i < entries.size(); ++i)
            {
                if (!used[i])
                {
                    remaining.push_back(entries[i]);
                }
            }
            if (!has_preferred_candidates)
            {
                std::stable_sort(remaining.begin(), remaining.end(),
                                 [](const Afs2Entry &lhs, const Afs2Entry &rhs)
                                 {
                                     return (lhs.end - lhs.begin) > (rhs.end - rhs.begin);
                                 });
                candidates.insert(candidates.end(), remaining.begin(), remaining.end());
            }

            for (const auto &entry : candidates)
            {
                if (entry.end <= entry.begin || entry.end > awb_bytes.size())
                {
                    continue;
                }

                std::vector<uint8_t> payload(entry.end - entry.begin, 0U);
                std::copy(awb_bytes.begin() + static_cast<std::ptrdiff_t>(entry.begin),
                          awb_bytes.begin() + static_cast<std::ptrdiff_t>(entry.end),
                          payload.begin());

                const AudioPayloadFormat payload_format =
                    detect_audio_payload_format(payload);
                if (payload_format == AudioPayloadFormat::kUnknown)
                {
                    // Keep decode strict for AWB entries to avoid ffmpeg auto-probe false
                    // positives that produce noisy output.
                    continue;
                }
                const auto demuxer_candidates = build_awb_entry_demuxer_candidates(payload);
                for (const auto &force_format : demuxer_candidates)
                {
                    const bool is_hca = force_format.has_value() && *force_format == "hca";
                    bool is_ehca = false;
                    if (is_hca)
                    {
                        is_hca_or_ehca_signature_at(payload, 0U, &is_ehca);
                    }
                    const std::optional<uint16_t> hca_encryption_type =
                        is_hca ? detect_hca_encryption_type(payload) : std::nullopt;
                    // Follow MaiChartManager's decode behavior:
                    // - prefer no-key for unencrypted HCA (ciph=0)
                    // - use keyed decode when stream declares encryption.
                    const bool prefer_keyed_decode =
                        (hca_encryption_type.has_value() && *hca_encryption_type != 0U) ||
                        (is_ehca && !hca_encryption_type.has_value());
                    const bool allow_keyed_attempt =
                        !hca_encryption_type.has_value() || *hca_encryption_type != 0U;
                    for (const auto &encoder : mp3_encoders)
                    {
                        if (is_hca)
                        {
                            const auto try_no_key_decode = [&]()
                            {
                                remove_file_if_exists(target_mp3);
                                return transcode_audio_stdin_to_mp3_ffmpeg(
                                    payload, target_mp3, encoder, force_format, nullptr);
                            };
                            const auto try_keyed_decode = [&]()
                            {
                                if (!allow_keyed_attempt)
                                {
                                    return false;
                                }
                                for (const auto &hca_decode_options :
                                     hca_decode_options_candidates)
                                {
                                    remove_file_if_exists(target_mp3);
                                    if (transcode_audio_stdin_to_mp3_ffmpeg(payload, target_mp3,
                                                                            encoder, force_format,
                                                                            &hca_decode_options))
                                    {
                                        return true;
                                    }
                                }
                                return false;
                            };

                            if (prefer_keyed_decode)
                            {
                                // For encrypted tracks, avoid no-key fallback because ffmpeg may
                                // still emit valid-looking noisy output with wrong decryption.
                                if (try_keyed_decode())
                                {
                                    return true;
                                }
                            }
                            else
                            {
                                if (try_no_key_decode() || try_keyed_decode())
                                {
                                    return true;
                                }
                            }
                            continue;
                        }

                        remove_file_if_exists(target_mp3);
                        if (transcode_audio_stdin_to_mp3_ffmpeg(payload, target_mp3, encoder,
                                                                force_format, nullptr))
                        {
                            return true;
                        }
                    }
                }
            }

            return false;
        }

        bool transcode_audio_to_mp3_ffmpeg(
            const std::filesystem::path &source,
            const std::filesystem::path &target_mp3,
            const std::vector<uint32_t> *preferred_awb_entry_ids = nullptr)
        {
            if (!file_non_empty(source))
            {
                return false;
            }
            if (!target_mp3.parent_path().empty())
            {
                std::filesystem::create_directories(target_mp3.parent_path());
            }

            const auto mp3_encoders = resolve_ffmpeg_mp3_encoders();
            if (mp3_encoders.empty())
            {
                return false;
            }

            const std::string source_ext = lower(source.extension().string());
            if (source_ext == ".awb")
            {
                if (transcode_awb_afs2_to_mp3_ffmpeg(source, target_mp3, mp3_encoders,
                                                     preferred_awb_entry_ids))
                {
                    return true;
                }

                // If this is a real AFS2 AWB container, avoid ffmpeg auto-probe fallback,
                // which may "succeed" with invalid noisy output.
                std::array<uint8_t, 4> head{};
                std::ifstream in(source, std::ios::binary);
                in.read(reinterpret_cast<char *>(head.data()),
                        static_cast<std::streamsize>(head.size()));
                if (in.gcount() == static_cast<std::streamsize>(head.size()) &&
                    head[0] == static_cast<uint8_t>('A') &&
                    head[1] == static_cast<uint8_t>('F') &&
                    head[2] == static_cast<uint8_t>('S') &&
                    head[3] == static_cast<uint8_t>('2'))
                {
                    return false;
                }
            }

            for (const auto &encoder : mp3_encoders)
            {
                remove_file_if_exists(target_mp3);
                if (transcode_audio_file_to_mp3_ffmpeg(source, target_mp3, encoder))
                {
                    return true;
                }
            }

            return false;
        }
    } // namespace

    bool media_video_shared_file_non_empty(const std::filesystem::path &path)
    {
        return file_non_empty(path);
    }

    std::string media_video_shared_lower(const std::string &s)
    {
        return lower(s);
    }

    std::filesystem::path media_video_shared_make_temp_work_dir()
    {
        return make_temp_work_dir();
    }

    bool media_video_shared_is_mp3_like_file(const std::filesystem::path &path)
    {
        return is_mp3_like_file(path);
    }

    bool media_video_shared_read_acb_stub_sidecar_awb_name(
        const std::filesystem::path &acb,
        std::string &awb_name_out,
        uint64_t &awb_size_out)
    {
        return read_acb_stub_sidecar_awb_name(acb, awb_name_out, awb_size_out);
    }

    bool media_video_shared_collect_preferred_awb_entry_ids(
        const std::filesystem::path &acb,
        const std::filesystem::path &awb,
        std::vector<uint32_t> &preferred_awb_entry_ids_out)
    {
        preferred_awb_entry_ids_out.clear();
        AcbMetadata acb_metadata;
        if (!(parse_acb_metadata(acb, acb_metadata) &&
              !acb_metadata.stream_awb_ids.empty()))
        {
            return false;
        }

        bool awb_name_matches = true;
        if (!acb_metadata.stream_awb_name.empty())
        {
            std::string expected = lower(acb_metadata.stream_awb_name);
            if (expected.size() > 4U &&
                expected.substr(expected.size() - 4U) == ".awb")
            {
                expected.resize(expected.size() - 4U);
            }
            const std::string awb_stem = lower(awb.stem().string());
            const std::string awb_file = lower(awb.filename().string());
            awb_name_matches = (expected == awb_stem) || (expected == awb_file);
        }
        if (!awb_name_matches)
        {
            return false;
        }

        preferred_awb_entry_ids_out = acb_metadata.stream_awb_ids;
        return true;
    }

    bool media_video_shared_transcode_audio_to_mp3_ffmpeg(
        const std::filesystem::path &source,
        const std::filesystem::path &target_mp3,
        const std::vector<uint32_t> *preferred_awb_entry_ids)
    {
        return transcode_audio_to_mp3_ffmpeg(source, target_mp3,
                                             preferred_awb_entry_ids);
    }

    bool media_video_shared_write_embedded_png(
        const std::filesystem::path &source,
        const std::filesystem::path &png_file)
    {
        return write_embedded_png(source, png_file);
    }

    std::vector<std::string> media_video_shared_resolve_ffmpeg_mp3_encoders()
    {
        return resolve_ffmpeg_mp3_encoders();
    }

    void media_video_shared_remove_file_if_exists(const std::filesystem::path &path)
    {
        remove_file_if_exists(path);
    }

    std::string media_video_shared_path_to_utf8(const std::filesystem::path &path)
    {
        return path_to_utf8(path);
    }

#if defined(_WIN32)
    std::wstring media_video_shared_widen_ascii(const std::string &value)
    {
        return widen_ascii(value);
    }

    void media_video_shared_append_audio_hwaccel_arg(
        std::vector<std::wstring> &args)
    {
        append_audio_hwaccel_arg(args);
    }

    bool media_video_shared_run_ffmpeg_process(
        const std::vector<std::wstring> &args)
    {
        return run_ffmpeg_process(args);
    }
#else
    void media_video_shared_append_audio_hwaccel_arg(
        std::vector<std::string> &args)
    {
        append_audio_hwaccel_arg(args);
    }

    bool media_video_shared_run_ffmpeg_process(
        const std::vector<std::string> &args)
    {
        return run_ffmpeg_process(args);
    }
#endif

    bool convert_dat_or_usm_to_mp4(const std::filesystem::path &source,
                                   const std::filesystem::path &target_mp4)
    {
        if (!file_non_empty(source))
        {
            return false;
        }

        if (convert_usm_to_mp4(source, target_mp4))
        {
            return true;
        }

        const auto h264_encoders = resolve_ffmpeg_h264_encoders();
        if (h264_encoders.empty())
        {
            return false;
        }

        for (const auto &encoder : h264_encoders)
        {
            remove_file_if_exists(target_mp4);
#if defined(_WIN32)
            std::vector<std::wstring> args = {L"-y", L"-loglevel", L"error"};
            append_hwaccel_arg(args);
            args.insert(args.end(),
                        {L"-i", source.wstring(), L"-an", L"-c:v", widen_ascii(encoder),
                         L"-pix_fmt", L"yuv420p", target_mp4.wstring()});
            const bool fallback_ok = run_ffmpeg_process(args);
#else
            std::vector<std::string> args = {"-y", "-loglevel", "error"};
            append_hwaccel_arg(args);
            args.insert(args.end(), {"-i", path_to_utf8(source), "-an", "-c:v", encoder,
                                     "-pix_fmt", "yuv420p", path_to_utf8(target_mp4)});
            const bool fallback_ok = run_ffmpeg_process(args);
#endif
            if (fallback_ok && file_non_empty(target_mp4))
            {
                return true;
            }
        }

        return false;
    }

    bool generate_single_frame_mp4_from_image(
        const std::filesystem::path &source_image,
        const std::filesystem::path &target_mp4)
    {
        if (!file_non_empty(source_image))
        {
            return false;
        }
        if (!target_mp4.parent_path().empty())
        {
            std::filesystem::create_directories(target_mp4.parent_path());
        }

        const auto h264_encoders = resolve_ffmpeg_h264_encoders();
        if (h264_encoders.empty())
        {
            return false;
        }

        for (const auto &encoder : h264_encoders)
        {
            remove_file_if_exists(target_mp4);
#if defined(_WIN32)
            std::vector<std::wstring> args = {L"-y", L"-loglevel", L"error"};
            append_hwaccel_arg(args);
            args.insert(args.end(),
                        {L"-loop", L"1", L"-i", source_image.wstring(), L"-frames:v",
                         L"1", L"-an", L"-c:v", widen_ascii(encoder), L"-pix_fmt",
                         L"yuv420p", target_mp4.wstring()});
            const bool ok = run_ffmpeg_process(args);
#else
            std::vector<std::string> args = {"-y", "-loglevel", "error"};
            append_hwaccel_arg(args);
            args.insert(args.end(), {"-loop", "1", "-i", path_to_utf8(source_image),
                                     "-frames:v", "1", "-an", "-c:v", encoder,
                                     "-pix_fmt", "yuv420p", path_to_utf8(target_mp4)});
            const bool ok = run_ffmpeg_process(args);
#endif
            if (ok && file_non_empty(target_mp4))
            {
                return true;
            }
        }

        return false;
    }

    bool generate_single_frame_black_mp4(const std::filesystem::path &target_mp4)
    {
        if (!target_mp4.parent_path().empty())
        {
            std::filesystem::create_directories(target_mp4.parent_path());
        }

        const auto h264_encoders = resolve_ffmpeg_h264_encoders();
        if (h264_encoders.empty())
        {
            return false;
        }

        for (const auto &encoder : h264_encoders)
        {
            remove_file_if_exists(target_mp4);
#if defined(_WIN32)
            std::vector<std::wstring> args = {L"-y", L"-loglevel", L"error"};
            append_hwaccel_arg(args);
            args.insert(args.end(),
                        {L"-f", L"lavfi", L"-i", L"color=c=black:s=1280x720:r=1",
                         L"-frames:v", L"1", L"-an", L"-c:v", widen_ascii(encoder),
                         L"-pix_fmt", L"yuv420p", target_mp4.wstring()});
            const bool ok = run_ffmpeg_process(args);
#else
            std::vector<std::string> args = {"-y", "-loglevel", "error"};
            append_hwaccel_arg(args);
            args.insert(args.end(),
                        {"-f", "lavfi", "-i", "color=c=black:s=1280x720:r=1",
                         "-frames:v", "1", "-an", "-c:v", encoder, "-pix_fmt",
                         "yuv420p", path_to_utf8(target_mp4)});
            const bool ok = run_ffmpeg_process(args);
#endif
            if (ok && file_non_empty(target_mp4))
            {
                return true;
            }
        }

        return false;
    }

    bool convert_mp4_to_dat(const std::filesystem::path &source_mp4,
                            const std::filesystem::path &target_dat)
    {
        if (!file_non_empty(source_mp4))
        {
            return false;
        }

        std::vector<uint8_t> ivf_bytes;
        const bool converted =
            transcode_mp4_to_vp9_ivf_bytes(source_mp4, ivf_bytes) &&
            build_minimal_dat_from_vp9_ivf(ivf_bytes, target_dat);

        return converted;
    }

} // namespace maiconv
