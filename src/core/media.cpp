#include "maiconv/core/media.hpp"

#include "maiconv/core/io.hpp"

namespace maiconv {
bool extract_unity_texture_bundle_to_png(const std::filesystem::path &ab_file,
                                         const std::filesystem::path &png_file);
}

extern "C" {
#include "layer3.h"
#include "libvgmstream.h"
}

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
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

#if MAICONV_HAS_LIBAV
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/mathematics.h>
#include <libavutil/opt.h>
}
#endif

namespace maiconv {
namespace {

constexpr std::array<uint8_t, 16> kAcbStubMagic = {
    static_cast<uint8_t>('M'), static_cast<uint8_t>('A'),
    static_cast<uint8_t>('I'), static_cast<uint8_t>('C'),
    static_cast<uint8_t>('O'), static_cast<uint8_t>('N'),
    static_cast<uint8_t>('V'), static_cast<uint8_t>('_'),
    static_cast<uint8_t>('A'), static_cast<uint8_t>('C'),
    static_cast<uint8_t>('B'), static_cast<uint8_t>('_'),
    static_cast<uint8_t>('S'), static_cast<uint8_t>('T'),
    static_cast<uint8_t>('U'), static_cast<uint8_t>('B')};

void write_u32_le(std::ofstream &out, uint32_t value) {
  const std::array<uint8_t, 4> bytes = {
      static_cast<uint8_t>(value & 0xFFU),
      static_cast<uint8_t>((value >> 8U) & 0xFFU),
      static_cast<uint8_t>((value >> 16U) & 0xFFU),
      static_cast<uint8_t>((value >> 24U) & 0xFFU)};
  out.write(reinterpret_cast<const char *>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
}

void write_u64_le(std::ofstream &out, uint64_t value) {
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
}

bool is_mp3_like_file(const std::filesystem::path &path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return false;
  }

  std::array<uint8_t, 3> head{};
  in.read(reinterpret_cast<char *>(head.data()),
          static_cast<std::streamsize>(head.size()));
  if (in.gcount() < 2) {
    return false;
  }

  if (in.gcount() == 3 && head[0] == static_cast<uint8_t>('I') &&
      head[1] == static_cast<uint8_t>('D') &&
      head[2] == static_cast<uint8_t>('3')) {
    return true;
  }

  return head[0] == 0xFFU && (head[1] & 0xE0U) == 0xE0U;
}

bool read_acb_stub_sidecar_awb_name(const std::filesystem::path &acb,
                                    std::string &awb_name_out,
                                    uint64_t &awb_size_out) {
  std::ifstream in(acb, std::ios::binary);
  if (!in) {
    return false;
  }

  std::array<uint8_t, 16> magic{};
  in.read(reinterpret_cast<char *>(magic.data()),
          static_cast<std::streamsize>(magic.size()));
  if (in.gcount() != static_cast<std::streamsize>(magic.size()) ||
      magic != kAcbStubMagic) {
    return false;
  }

  std::array<uint8_t, 4> u32{};
  in.read(reinterpret_cast<char *>(u32.data()),
          static_cast<std::streamsize>(u32.size()));
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
  in.read(reinterpret_cast<char *>(u64.data()),
          static_cast<std::streamsize>(u64.size()));
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

  in.read(reinterpret_cast<char *>(u32.data()),
          static_cast<std::streamsize>(u32.size()));
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

uint32_t read_u32_be(const uint8_t *p) {
  return (static_cast<uint32_t>(p[0]) << 24U) |
         (static_cast<uint32_t>(p[1]) << 16U) |
         (static_cast<uint32_t>(p[2]) << 8U) | static_cast<uint32_t>(p[3]);
}

uint16_t read_u16_le(const uint8_t *p) {
  return static_cast<uint16_t>(p[0]) |
         static_cast<uint16_t>(static_cast<uint16_t>(p[1]) << 8U);
}

uint16_t read_u16_be(const uint8_t *p) {
  return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8U) |
                               static_cast<uint16_t>(p[1]));
}

bool file_non_empty(const std::filesystem::path &path) {
  return std::filesystem::exists(path) &&
         std::filesystem::is_regular_file(path) &&
         std::filesystem::file_size(path) > 0;
}

class TempWorkspacePool {
public:
  TempWorkspacePool() {
    const auto now =
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch())
            .count();
    root_ = std::filesystem::temp_directory_path() /
            ("maiconv_media_pool_" + std::to_string(now));
    std::filesystem::create_directories(root_);
  }

  ~TempWorkspacePool() {
    std::error_code ec;
    std::filesystem::remove_all(root_, ec);
  }

  std::filesystem::path make_workspace() {
    const auto id = counter_.fetch_add(1, std::memory_order_relaxed);
    const auto dir = root_ / ("workspace_" + std::to_string(id));
    std::filesystem::create_directories(dir);
    return dir;
  }

private:
  std::filesystem::path root_;
  std::atomic<unsigned long long> counter_{0};
};

TempWorkspacePool &temp_workspace_pool() {
  static TempWorkspacePool pool;
  return pool;
}

std::filesystem::path make_temp_work_dir() {
  return temp_workspace_pool().make_workspace();
}

std::string path_to_utf8(const std::filesystem::path &path) {
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

#if defined(_WIN32)
std::wstring resolve_ffmpeg_executable();

std::wstring quote_windows_argument(const std::wstring &arg) {
  if (arg.empty()) {
    return L"\"\"";
  }
  if (arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
    return arg;
  }

  std::wstring out;
  out.reserve(arg.size() + 2);
  out.push_back(L'"');

  std::size_t backslashes = 0;
  for (const wchar_t ch : arg) {
    if (ch == L'\\') {
      ++backslashes;
      continue;
    }
    if (ch == L'"') {
      out.append(backslashes * 2 + 1, L'\\');
      out.push_back(L'"');
      backslashes = 0;
      continue;
    }
    if (backslashes > 0) {
      out.append(backslashes, L'\\');
      backslashes = 0;
    }
    out.push_back(ch);
  }
  if (backslashes > 0) {
    out.append(backslashes * 2, L'\\');
  }
  out.push_back(L'"');
  return out;
}

std::wstring build_windows_command_line(const std::vector<std::wstring> &args) {
  std::wstring command_line =
      quote_windows_argument(resolve_ffmpeg_executable());
  for (const auto &arg : args) {
    command_line.push_back(L' ');
    command_line += quote_windows_argument(arg);
  }
  return command_line;
}

bool wait_process_success(HANDLE process_handle, HANDLE thread_handle) {
  const DWORD wait_rc = WaitForSingleObject(process_handle, INFINITE);
  DWORD exit_code = 1;
  if (wait_rc == WAIT_OBJECT_0) {
    GetExitCodeProcess(process_handle, &exit_code);
  }

  CloseHandle(thread_handle);
  CloseHandle(process_handle);
  return wait_rc == WAIT_OBJECT_0 && exit_code == 0;
}

std::wstring resolve_ffmpeg_executable() {
  static std::once_flag once;
  static std::wstring cached;
  std::call_once(once, []() {
    const wchar_t *env_path = _wgetenv(L"MAICONV_FFMPEG");
    if (env_path != nullptr && env_path[0] != L'\0') {
      cached = env_path;
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

    cached = file;
  });
  return cached;
}

[[maybe_unused]] bool
run_ffmpeg_process(const std::vector<std::wstring> &args) {
  if (args.empty()) {
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
  if (!created) {
    return false;
  }

  return wait_process_success(process.hProcess, process.hThread);
}

bool run_ffmpeg_capture_stdout(const std::vector<std::wstring> &args,
                               std::vector<uint8_t> &stdout_bytes) {
  if (args.empty()) {
    return false;
  }

  stdout_bytes.clear();

  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;

  HANDLE out_read = nullptr;
  HANDLE out_write = nullptr;
  if (!CreatePipe(&out_read, &out_write, &sa, 0)) {
    return false;
  }
  if (!SetHandleInformation(out_read, HANDLE_FLAG_INHERIT, 0)) {
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
  if (in_null == INVALID_HANDLE_VALUE || err_null == INVALID_HANDLE_VALUE) {
    if (in_null != INVALID_HANDLE_VALUE) {
      CloseHandle(in_null);
    }
    if (err_null != INVALID_HANDLE_VALUE) {
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

  if (!created) {
    CloseHandle(out_read);
    return false;
  }

  std::array<uint8_t, 32768> buffer{};
  DWORD read_count = 0;
  while (ReadFile(out_read, buffer.data(), static_cast<DWORD>(buffer.size()),
                  &read_count, nullptr) &&
         read_count > 0) {
    stdout_bytes.insert(stdout_bytes.end(), buffer.begin(),
                        buffer.begin() +
                            static_cast<std::ptrdiff_t>(read_count));
  }
  CloseHandle(out_read);

  const bool ok = wait_process_success(process.hProcess, process.hThread);
  return ok;
}

bool run_ffmpeg_feed_stdin(const std::vector<std::wstring> &args,
                           const std::vector<uint8_t> &stdin_bytes) {
  if (args.empty()) {
    return false;
  }

  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;

  HANDLE in_read = nullptr;
  HANDLE in_write = nullptr;
  if (!CreatePipe(&in_read, &in_write, &sa, 0)) {
    return false;
  }
  if (!SetHandleInformation(in_write, HANDLE_FLAG_INHERIT, 0)) {
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
  if (out_null == INVALID_HANDLE_VALUE || err_null == INVALID_HANDLE_VALUE) {
    if (out_null != INVALID_HANDLE_VALUE) {
      CloseHandle(out_null);
    }
    if (err_null != INVALID_HANDLE_VALUE) {
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

  if (!created) {
    CloseHandle(in_write);
    return false;
  }

  bool write_ok = true;
  std::size_t offset = 0;
  while (offset < stdin_bytes.size()) {
    const auto remaining = stdin_bytes.size() - offset;
    const DWORD chunk =
        static_cast<DWORD>(std::min<std::size_t>(remaining, 1U << 20));
    DWORD written = 0;
    if (!WriteFile(in_write,
                   stdin_bytes.data() + static_cast<std::ptrdiff_t>(offset),
                   chunk, &written, nullptr) ||
        written == 0) {
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
std::string resolve_ffmpeg_executable() {
  static std::once_flag once;
  static std::string cached;
  std::call_once(once, []() {
    const char *env_path = std::getenv("MAICONV_FFMPEG");
    if (env_path != nullptr && env_path[0] != '\0') {
      cached = env_path;
      return;
    }
    cached = "ffmpeg";
  });
  return cached;
}

[[maybe_unused]] bool run_ffmpeg_process(const std::vector<std::string> &args) {
  if (args.empty()) {
    return false;
  }

  std::vector<std::string> argv_storage;
  argv_storage.reserve(args.size() + 1);
  argv_storage.push_back(resolve_ffmpeg_executable());
  argv_storage.insert(argv_storage.end(), args.begin(), args.end());

  std::vector<char *> argv;
  argv.reserve(argv_storage.size() + 1);
  for (auto &token : argv_storage) {
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
  if (spawn_rc != 0) {
    return false;
  }

  int status = 0;
  int wait_rc = 0;
  do {
    wait_rc = waitpid(pid, &status, 0);
  } while (wait_rc == -1 && errno == EINTR);
  if (wait_rc == -1) {
    return false;
  }

  return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

bool run_ffmpeg_capture_stdout(const std::vector<std::string> &args,
                               std::vector<uint8_t> &stdout_bytes) {
  if (args.empty()) {
    return false;
  }
  stdout_bytes.clear();

  int out_pipe[2] = {-1, -1};
  if (pipe(out_pipe) != 0) {
    return false;
  }

  const int in_null = open("/dev/null", O_RDONLY);
  const int err_null = open("/dev/null", O_WRONLY);
  if (in_null < 0 || err_null < 0) {
    if (in_null >= 0) {
      close(in_null);
    }
    if (err_null >= 0) {
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
  for (auto &token : argv_storage) {
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

  if (spawn_rc != 0) {
    close(out_pipe[0]);
    return false;
  }

  std::array<uint8_t, 32768> buffer{};
  while (true) {
    const ssize_t n = read(out_pipe[0], buffer.data(), buffer.size());
    if (n == 0) {
      break;
    }
    if (n < 0) {
      if (errno == EINTR) {
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
  do {
    wait_rc = waitpid(pid, &status, 0);
  } while (wait_rc == -1 && errno == EINTR);
  if (wait_rc == -1) {
    return false;
  }

  return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

bool run_ffmpeg_feed_stdin(const std::vector<std::string> &args,
                           const std::vector<uint8_t> &stdin_bytes) {
  if (args.empty()) {
    return false;
  }

  int in_pipe[2] = {-1, -1};
  if (pipe(in_pipe) != 0) {
    return false;
  }

  const int out_null = open("/dev/null", O_WRONLY);
  const int err_null = open("/dev/null", O_WRONLY);
  if (out_null < 0 || err_null < 0) {
    if (out_null >= 0) {
      close(out_null);
    }
    if (err_null >= 0) {
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
  for (auto &token : argv_storage) {
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

  if (spawn_rc != 0) {
    close(in_pipe[1]);
    return false;
  }

  bool write_ok = true;
  std::size_t offset = 0;
  while (offset < stdin_bytes.size()) {
    const auto remaining = stdin_bytes.size() - offset;
    const size_t chunk = std::min<std::size_t>(remaining, 1U << 20);
    const ssize_t written =
        write(in_pipe[1],
              stdin_bytes.data() + static_cast<std::ptrdiff_t>(offset), chunk);
    if (written < 0) {
      if (errno == EINTR) {
        continue;
      }
      write_ok = false;
      break;
    }
    if (written == 0) {
      write_ok = false;
      break;
    }
    offset += static_cast<std::size_t>(written);
  }
  close(in_pipe[1]);

  int status = 0;
  int wait_rc = 0;
  do {
    wait_rc = waitpid(pid, &status, 0);
  } while (wait_rc == -1 && errno == EINTR);
  if (wait_rc == -1) {
    return false;
  }

  return write_ok && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}
#endif

bool read_exact(std::istream &in, uint8_t *out, std::size_t size) {
  if (size == 0) {
    return true;
  }
  in.read(reinterpret_cast<char *>(out), static_cast<std::streamsize>(size));
  return static_cast<std::size_t>(in.gcount()) == size;
}

template <std::size_t N>
bool starts_with(const std::vector<uint8_t> &data,
                 const std::array<uint8_t, N> &sig) {
  return data.size() >= sig.size() &&
         std::equal(sig.begin(), sig.end(), data.begin());
}

std::optional<int> pick_supported_bitrate(int sample_rate) {
  constexpr std::array<int, 11> kCandidates = {192, 160, 128, 112, 96, 80,
                                               64,  56,  48,  40,  32};
  for (const int br : kCandidates) {
    if (shine_check_config(sample_rate, br) >= 0) {
      return br;
    }
  }
  return std::nullopt;
}

shine_t create_shine_encoder(int sample_rate, int channels,
                             int *samples_per_pass) {
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

bool decode_vgmstream_to_mp3_subsong(const std::filesystem::path &input,
                                     int subsong,
                                     const std::filesystem::path &target_mp3) {
  if (!file_non_empty(input)) {
    return false;
  }

  libstreamfile_t *sf = libstreamfile_open_from_stdio(input.string().c_str());
  if (sf == nullptr) {
    return false;
  }

  libvgmstream_t *lib = libvgmstream_init();
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

  std::vector<uint8_t> encoded_buffer;
  encoded_buffer.reserve(256 * 1024);

  const auto flush_encoded_buffer = [&]() {
    if (encoded_buffer.empty()) {
      return;
    }
    out.write(reinterpret_cast<const char *>(encoded_buffer.data()),
              static_cast<std::streamsize>(encoded_buffer.size()));
    encoded_buffer.clear();
  };

  std::vector<int16_t> pcm(static_cast<std::size_t>(samples_per_pass) *
                               static_cast<std::size_t>(channels),
                           0);

  while (lib->decoder != nullptr && !lib->decoder->done) {
    const int err = libvgmstream_fill(lib, pcm.data(), samples_per_pass);
    if (err < 0) {
      shine_close(enc);
      libvgmstream_free(lib);
      return false;
    }

    int written = 0;
    unsigned char *packet =
        shine_encode_buffer_interleaved(enc, pcm.data(), &written);
    if (packet == nullptr || written < 0) {
      shine_close(enc);
      libvgmstream_free(lib);
      return false;
    }
    if (written > 0) {
      const auto begin = packet;
      const auto end = packet + written;
      encoded_buffer.insert(encoded_buffer.end(), begin, end);
      if (encoded_buffer.size() >= 256 * 1024) {
        flush_encoded_buffer();
        if (!out.good()) {
          shine_close(enc);
          libvgmstream_free(lib);
          return false;
        }
      }
    }
  }

  int flushed = 0;
  unsigned char *tail = shine_flush(enc, &flushed);
  if (tail != nullptr && flushed > 0) {
    const auto begin = tail;
    const auto end = tail + flushed;
    encoded_buffer.insert(encoded_buffer.end(), begin, end);
  }

  flush_encoded_buffer();
  if (!out.good()) {
    shine_close(enc);
    libvgmstream_free(lib);
    return false;
  }

  shine_close(enc);
  libvgmstream_free(lib);

  out.flush();
  return out.good() && file_non_empty(target_mp3);
}

bool decode_vgmstream_to_mp3(const std::filesystem::path &input,
                             const std::filesystem::path &target_mp3) {
  return decode_vgmstream_to_mp3_subsong(input, 0, target_mp3);
}

std::optional<int>
find_longest_vgmstream_subsong(const std::filesystem::path &input) {
  if (!file_non_empty(input)) {
    return std::nullopt;
  }

  libstreamfile_t *sf = libstreamfile_open_from_stdio(input.string().c_str());
  if (sf == nullptr) {
    return std::nullopt;
  }

  libvgmstream_t *lib = libvgmstream_init();
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

bool write_embedded_png(const std::filesystem::path &source,
                        const std::filesystem::path &png_file) {
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
  in.read(reinterpret_cast<char *>(bytes.data()),
          static_cast<std::streamsize>(bytes.size()));
  if (!in) {
    return false;
  }

  constexpr std::array<uint8_t, 8> kPngSig = {0x89U, 0x50U, 0x4EU, 0x47U,
                                              0x0DU, 0x0AU, 0x1AU, 0x0AU};

  for (std::size_t start = 0; start + kPngSig.size() < bytes.size(); ++start) {
    if (!std::equal(kPngSig.begin(), kPngSig.end(),
                    bytes.begin() + static_cast<std::ptrdiff_t>(start))) {
      continue;
    }

    std::size_t chunk = start + kPngSig.size();
    while (chunk + 8 <= bytes.size()) {
      const uint32_t length = read_u32_be(bytes.data() + chunk);
      const std::size_t data_start = chunk + 8;
      const std::size_t data_end =
          data_start + static_cast<std::size_t>(length);
      const std::size_t crc_end = data_end + 4;
      if (crc_end > bytes.size()) {
        break;
      }

      const std::string type(
          reinterpret_cast<const char *>(bytes.data() + chunk + 4), 4);
      if (type == "IEND") {
        if (!png_file.parent_path().empty()) {
          std::filesystem::create_directories(png_file.parent_path());
        }
        std::ofstream out(png_file, std::ios::binary | std::ios::trunc);
        if (!out) {
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
    const uint16_t header_size = read_u16_le(data.data() + i + 6U);
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

VideoCodec detect_video_codec(const std::vector<uint8_t> &data) {
  if (is_vp9_ivf_stream(data)) {
    return VideoCodec::kVp9Ivf;
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

    const uint32_t chunk_size_after_header = read_u32_be(header.data() + 4);
    const uint32_t payload_offset = header[9];
    const uint32_t padding_size = read_u16_be(header.data() + 10);
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

    std::vector<uint8_t> decoded =
        decrypt_usm_video_packet(payload, keys.video);
    ChannelData &channel = channels[channel_number];

    if (!channel.seen) {
      channel.seen = true;
      channel_order.push_back(channel_number);
    }

    channel.data.insert(channel.data.end(), decoded.begin(), decoded.end());
    if (channel.codec == VideoCodec::kUnknown &&
        is_vp9_ivf_stream(channel.data)) {
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

#if MAICONV_HAS_LIBAV
struct MemoryAvioState {
  const uint8_t *data = nullptr;
  std::size_t size = 0;
  std::size_t offset = 0;
};

int avio_read_from_memory(void *opaque, uint8_t *buf, int buf_size) {
  if (opaque == nullptr || buf == nullptr || buf_size <= 0) {
    return AVERROR(EINVAL);
  }
  auto *state = static_cast<MemoryAvioState *>(opaque);
  if (state->offset >= state->size) {
    return AVERROR_EOF;
  }

  const auto remaining = state->size - state->offset;
  const auto to_copy =
      std::min<std::size_t>(remaining, static_cast<std::size_t>(buf_size));
  std::memcpy(buf, state->data + static_cast<std::ptrdiff_t>(state->offset),
              to_copy);
  state->offset += to_copy;
  return static_cast<int>(to_copy);
}

int64_t avio_seek_in_memory(void *opaque, int64_t offset, int whence) {
  if (opaque == nullptr) {
    return AVERROR(EINVAL);
  }
  auto *state = static_cast<MemoryAvioState *>(opaque);
  if ((whence & AVSEEK_SIZE) != 0) {
    return static_cast<int64_t>(state->size);
  }

  std::int64_t base = 0;
  switch (whence) {
  case SEEK_SET:
    base = 0;
    break;
  case SEEK_CUR:
    base = static_cast<std::int64_t>(state->offset);
    break;
  case SEEK_END:
    base = static_cast<std::int64_t>(state->size);
    break;
  default:
    return AVERROR(EINVAL);
  }

  const std::int64_t next = base + offset;
  if (next < 0 || next > static_cast<std::int64_t>(state->size)) {
    return AVERROR(EINVAL);
  }
  state->offset = static_cast<std::size_t>(next);
  return next;
}

const AVCodec *find_encoder_with_fallback(const char *preferred_name,
                                          AVCodecID codec_id) {
  if (preferred_name != nullptr) {
    if (const AVCodec *named = avcodec_find_encoder_by_name(preferred_name)) {
      return named;
    }
  }
  return avcodec_find_encoder(codec_id);
}

bool encoder_supports_pix_fmt(const AVCodec *encoder, AVPixelFormat pix_fmt) {
  if (encoder == nullptr) {
    return false;
  }
  if (encoder->pix_fmts == nullptr) {
    return true;
  }
  for (const AVPixelFormat *it = encoder->pix_fmts; *it != AV_PIX_FMT_NONE;
       ++it) {
    if (*it == pix_fmt) {
      return true;
    }
  }
  return false;
}

AVRational pick_frame_rate(const AVStream *input_stream,
                           const AVCodecContext *decoder_ctx) {
  if (input_stream != nullptr && input_stream->avg_frame_rate.num > 0 &&
      input_stream->avg_frame_rate.den > 0) {
    return input_stream->avg_frame_rate;
  }
  if (input_stream != nullptr && input_stream->r_frame_rate.num > 0 &&
      input_stream->r_frame_rate.den > 0) {
    return input_stream->r_frame_rate;
  }
  if (decoder_ctx != nullptr && decoder_ctx->framerate.num > 0 &&
      decoder_ctx->framerate.den > 0) {
    return decoder_ctx->framerate;
  }
  return AVRational{30, 1};
}

bool open_video_decoder(AVFormatContext *input_ctx, int *video_stream_index_out,
                        AVCodecContext **decoder_ctx_out) {
  if (input_ctx == nullptr || video_stream_index_out == nullptr ||
      decoder_ctx_out == nullptr) {
    return false;
  }
  *video_stream_index_out = -1;
  *decoder_ctx_out = nullptr;

  const int video_stream_index =
      av_find_best_stream(input_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
  if (video_stream_index < 0) {
    return false;
  }
  AVStream *input_stream = input_ctx->streams[video_stream_index];
  if (input_stream == nullptr || input_stream->codecpar == nullptr) {
    return false;
  }

  const AVCodec *decoder =
      avcodec_find_decoder(input_stream->codecpar->codec_id);
  if (decoder == nullptr) {
    return false;
  }

  AVCodecContext *decoder_ctx = avcodec_alloc_context3(decoder);
  if (decoder_ctx == nullptr) {
    return false;
  }
  if (avcodec_parameters_to_context(decoder_ctx, input_stream->codecpar) < 0) {
    avcodec_free_context(&decoder_ctx);
    return false;
  }
  if (avcodec_open2(decoder_ctx, decoder, nullptr) < 0) {
    avcodec_free_context(&decoder_ctx);
    return false;
  }

  *video_stream_index_out = video_stream_index;
  *decoder_ctx_out = decoder_ctx;
  return true;
}

bool open_video_encoder(const AVCodec *encoder, AVFormatContext *output_ctx,
                        AVStream *output_stream,
                        const AVCodecContext *decoder_ctx,
                        AVRational source_time_base,
                        AVCodecContext **encoder_ctx_out) {
  if (encoder == nullptr || output_ctx == nullptr || output_stream == nullptr ||
      decoder_ctx == nullptr || encoder_ctx_out == nullptr) {
    return false;
  }
  *encoder_ctx_out = nullptr;

  AVCodecContext *encoder_ctx = avcodec_alloc_context3(encoder);
  if (encoder_ctx == nullptr) {
    return false;
  }

  encoder_ctx->width = decoder_ctx->width;
  encoder_ctx->height = decoder_ctx->height;
  encoder_ctx->sample_aspect_ratio = decoder_ctx->sample_aspect_ratio;

  AVPixelFormat encoder_pix_fmt = decoder_ctx->pix_fmt;
  if (encoder_pix_fmt == AV_PIX_FMT_NONE) {
    encoder_pix_fmt = encoder->pix_fmts != nullptr ? encoder->pix_fmts[0]
                                                   : AV_PIX_FMT_YUV420P;
  }
  if (!encoder_supports_pix_fmt(encoder, encoder_pix_fmt)) {
    if (encoder->pix_fmts == nullptr) {
      avcodec_free_context(&encoder_ctx);
      return false;
    }
    encoder_pix_fmt = encoder->pix_fmts[0];
  }
  encoder_ctx->pix_fmt = encoder_pix_fmt;

  const AVRational frame_rate = pick_frame_rate(nullptr, decoder_ctx);
  encoder_ctx->framerate = frame_rate;
  encoder_ctx->time_base = av_inv_q(frame_rate);
  if (encoder_ctx->time_base.num <= 0 || encoder_ctx->time_base.den <= 0) {
    encoder_ctx->time_base = source_time_base;
  }
  if (encoder_ctx->time_base.num <= 0 || encoder_ctx->time_base.den <= 0) {
    encoder_ctx->time_base = AVRational{1, 30};
  }

  encoder_ctx->bit_rate =
      decoder_ctx->bit_rate > 0 ? decoder_ctx->bit_rate : 2'000'000;
  if ((output_ctx->oformat->flags & AVFMT_GLOBALHEADER) != 0) {
    encoder_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
  }

  if (avcodec_open2(encoder_ctx, encoder, nullptr) < 0) {
    avcodec_free_context(&encoder_ctx);
    return false;
  }
  if (avcodec_parameters_from_context(output_stream->codecpar, encoder_ctx) <
      0) {
    avcodec_free_context(&encoder_ctx);
    return false;
  }
  output_stream->time_base = encoder_ctx->time_base;
  *encoder_ctx_out = encoder_ctx;
  return true;
}

bool transcode_video_stream(AVFormatContext *input_ctx, int video_stream_index,
                            AVCodecContext *decoder_ctx,
                            AVFormatContext *output_ctx,
                            AVCodecContext *encoder_ctx,
                            AVStream *output_stream) {
  if (input_ctx == nullptr || video_stream_index < 0 ||
      decoder_ctx == nullptr || output_ctx == nullptr ||
      encoder_ctx == nullptr || output_stream == nullptr) {
    return false;
  }

  AVPacket *input_packet = av_packet_alloc();
  AVPacket *output_packet = av_packet_alloc();
  AVFrame *frame = av_frame_alloc();
  if (input_packet == nullptr || output_packet == nullptr || frame == nullptr) {
    if (input_packet != nullptr) {
      av_packet_free(&input_packet);
    }
    if (output_packet != nullptr) {
      av_packet_free(&output_packet);
    }
    if (frame != nullptr) {
      av_frame_free(&frame);
    }
    return false;
  }

  const AVRational source_time_base =
      input_ctx->streams[video_stream_index]->time_base;
  int64_t next_pts = 0;
  bool success = true;

  const auto encode_frame = [&](AVFrame *input_frame) -> bool {
    AVFrame *frame_to_send = input_frame;
    if (frame_to_send != nullptr) {
      if (frame_to_send->format != encoder_ctx->pix_fmt ||
          frame_to_send->width != encoder_ctx->width ||
          frame_to_send->height != encoder_ctx->height) {
        return false;
      }

      int64_t pts = frame_to_send->best_effort_timestamp;
      if (pts == AV_NOPTS_VALUE) {
        pts = frame_to_send->pts;
      }
      if (pts == AV_NOPTS_VALUE) {
        frame_to_send->pts = next_pts++;
      } else {
        frame_to_send->pts =
            av_rescale_q(pts, source_time_base, encoder_ctx->time_base);
        next_pts = frame_to_send->pts + 1;
      }
    }

    int rc = avcodec_send_frame(encoder_ctx, frame_to_send);
    if (rc < 0) {
      return false;
    }

    while (true) {
      rc = avcodec_receive_packet(encoder_ctx, output_packet);
      if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF) {
        break;
      }
      if (rc < 0) {
        return false;
      }

      av_packet_rescale_ts(output_packet, encoder_ctx->time_base,
                           output_stream->time_base);
      output_packet->stream_index = output_stream->index;
      if (av_interleaved_write_frame(output_ctx, output_packet) < 0) {
        av_packet_unref(output_packet);
        return false;
      }
      av_packet_unref(output_packet);
    }
    return true;
  };

  while (success) {
    const int read_rc = av_read_frame(input_ctx, input_packet);
    if (read_rc == AVERROR_EOF) {
      break;
    }
    if (read_rc < 0) {
      success = false;
      break;
    }
    if (input_packet->stream_index != video_stream_index) {
      av_packet_unref(input_packet);
      continue;
    }

    int rc = avcodec_send_packet(decoder_ctx, input_packet);
    av_packet_unref(input_packet);
    if (rc < 0) {
      success = false;
      break;
    }

    while (true) {
      rc = avcodec_receive_frame(decoder_ctx, frame);
      if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF) {
        break;
      }
      if (rc < 0 || !encode_frame(frame)) {
        success = false;
        break;
      }
      av_frame_unref(frame);
    }
  }

  if (success) {
    int rc = avcodec_send_packet(decoder_ctx, nullptr);
    if (rc < 0) {
      success = false;
    }
    while (success) {
      rc = avcodec_receive_frame(decoder_ctx, frame);
      if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF) {
        break;
      }
      if (rc < 0 || !encode_frame(frame)) {
        success = false;
        break;
      }
      av_frame_unref(frame);
    }
  }

  if (success && !encode_frame(nullptr)) {
    success = false;
  }

  av_frame_free(&frame);
  av_packet_free(&output_packet);
  av_packet_free(&input_packet);
  return success;
}

bool transcode_mp4_to_vp9_ivf_libav(const std::filesystem::path &source_mp4,
                                    std::vector<uint8_t> &target_ivf) {
  target_ivf.clear();
  const std::string source_mp4_utf8 = path_to_utf8(source_mp4);
  AVFormatContext *input_ctx = nullptr;
  AVFormatContext *output_ctx = nullptr;
  AVCodecContext *decoder_ctx = nullptr;
  AVCodecContext *encoder_ctx = nullptr;
  AVStream *output_stream = nullptr;
  AVStream *input_stream = nullptr;
  const AVCodec *encoder = nullptr;
  int video_stream_index = -1;
  uint8_t *dyn_buffer = nullptr;
  bool success = false;

  if (avformat_open_input(&input_ctx, source_mp4_utf8.c_str(), nullptr,
                          nullptr) < 0 ||
      avformat_find_stream_info(input_ctx, nullptr) < 0) {
    goto cleanup;
  }

  if (!open_video_decoder(input_ctx, &video_stream_index, &decoder_ctx)) {
    goto cleanup;
  }
  input_stream = input_ctx->streams[video_stream_index];
  if (input_stream == nullptr) {
    goto cleanup;
  }

  if (avformat_alloc_output_context2(&output_ctx, nullptr, "ivf", nullptr) <
          0 ||
      output_ctx == nullptr) {
    goto cleanup;
  }
  if (avio_open_dyn_buf(&output_ctx->pb) < 0) {
    goto cleanup;
  }

  encoder = find_encoder_with_fallback("libvpx-vp9", AV_CODEC_ID_VP9);
  if (encoder == nullptr) {
    goto cleanup;
  }

  output_stream = avformat_new_stream(output_ctx, nullptr);
  if (output_stream == nullptr) {
    goto cleanup;
  }

  if (!open_video_encoder(encoder, output_ctx, output_stream, decoder_ctx,
                          input_stream->time_base, &encoder_ctx)) {
    goto cleanup;
  }

  if (avformat_write_header(output_ctx, nullptr) < 0) {
    goto cleanup;
  }
  if (!transcode_video_stream(input_ctx, video_stream_index, decoder_ctx,
                              output_ctx, encoder_ctx, output_stream)) {
    goto cleanup;
  }
  if (av_write_trailer(output_ctx) < 0) {
    goto cleanup;
  }

  {
    const int dyn_size = avio_close_dyn_buf(output_ctx->pb, &dyn_buffer);
    output_ctx->pb = nullptr;
    if (dyn_size <= 0 || dyn_buffer == nullptr) {
      goto cleanup;
    }
    target_ivf.assign(dyn_buffer, dyn_buffer + dyn_size);
    av_free(dyn_buffer);
    dyn_buffer = nullptr;
  }

  success = is_vp9_ivf_stream(target_ivf);

cleanup:
  if (dyn_buffer != nullptr) {
    av_free(dyn_buffer);
  }
  if (encoder_ctx != nullptr) {
    avcodec_free_context(&encoder_ctx);
  }
  if (decoder_ctx != nullptr) {
    avcodec_free_context(&decoder_ctx);
  }
  if (output_ctx != nullptr) {
    if (output_ctx->pb != nullptr) {
      uint8_t *throwaway = nullptr;
      const int throwaway_size = avio_close_dyn_buf(output_ctx->pb, &throwaway);
      output_ctx->pb = nullptr;
      if (throwaway_size > 0 && throwaway != nullptr) {
        av_free(throwaway);
      }
    }
    avformat_free_context(output_ctx);
  }
  if (input_ctx != nullptr) {
    avformat_close_input(&input_ctx);
  }
  return success;
}

bool transcode_vp9_ivf_to_h264_mp4_libav(
    const std::vector<uint8_t> &source_ivf,
    const std::filesystem::path &target_mp4) {
  if (source_ivf.empty()) {
    return false;
  }
  const std::string target_mp4_utf8 = path_to_utf8(target_mp4);

  AVFormatContext *input_ctx = avformat_alloc_context();
  AVFormatContext *output_ctx = nullptr;
  AVCodecContext *decoder_ctx = nullptr;
  AVCodecContext *encoder_ctx = nullptr;
  AVIOContext *input_avio = nullptr;
  unsigned char *input_avio_buffer = nullptr;
  MemoryAvioState memory_state{};
  AVStream *output_stream = nullptr;
  AVStream *input_stream = nullptr;
  const AVCodec *encoder = nullptr;
  const AVInputFormat *ivf_input = nullptr;
  int video_stream_index = -1;
  bool success = false;

  if (input_ctx == nullptr) {
    goto cleanup;
  }

  memory_state.data = source_ivf.data();
  memory_state.size = source_ivf.size();
  memory_state.offset = 0;

  input_avio_buffer = static_cast<unsigned char *>(av_malloc(32 * 1024));
  if (input_avio_buffer == nullptr) {
    goto cleanup;
  }
  input_avio =
      avio_alloc_context(input_avio_buffer, 32 * 1024, 0, &memory_state,
                         &avio_read_from_memory, nullptr, &avio_seek_in_memory);
  if (input_avio == nullptr) {
    goto cleanup;
  }
  input_ctx->pb = input_avio;
  input_ctx->flags |= AVFMT_FLAG_CUSTOM_IO;

  ivf_input = av_find_input_format("ivf");
  if (avformat_open_input(&input_ctx, nullptr, ivf_input, nullptr) < 0 ||
      avformat_find_stream_info(input_ctx, nullptr) < 0) {
    goto cleanup;
  }

  if (!open_video_decoder(input_ctx, &video_stream_index, &decoder_ctx)) {
    goto cleanup;
  }
  input_stream = input_ctx->streams[video_stream_index];
  if (input_stream == nullptr) {
    goto cleanup;
  }

  if (!target_mp4.parent_path().empty()) {
    std::filesystem::create_directories(target_mp4.parent_path());
  }
  if (avformat_alloc_output_context2(&output_ctx, nullptr, nullptr,
                                     target_mp4_utf8.c_str()) < 0 ||
      output_ctx == nullptr) {
    goto cleanup;
  }

  encoder = find_encoder_with_fallback("libx264", AV_CODEC_ID_H264);
  if (encoder == nullptr) {
    goto cleanup;
  }

  output_stream = avformat_new_stream(output_ctx, nullptr);
  if (output_stream == nullptr) {
    goto cleanup;
  }

  if (!open_video_encoder(encoder, output_ctx, output_stream, decoder_ctx,
                          input_stream->time_base, &encoder_ctx)) {
    goto cleanup;
  }

  if (!(output_ctx->oformat->flags & AVFMT_NOFILE)) {
    if (avio_open(&output_ctx->pb, target_mp4_utf8.c_str(), AVIO_FLAG_WRITE) <
        0) {
      goto cleanup;
    }
  }

  if (avformat_write_header(output_ctx, nullptr) < 0) {
    goto cleanup;
  }
  if (!transcode_video_stream(input_ctx, video_stream_index, decoder_ctx,
                              output_ctx, encoder_ctx, output_stream)) {
    goto cleanup;
  }
  if (av_write_trailer(output_ctx) < 0) {
    goto cleanup;
  }

  success = file_non_empty(target_mp4);

cleanup:
  if (encoder_ctx != nullptr) {
    avcodec_free_context(&encoder_ctx);
  }
  if (decoder_ctx != nullptr) {
    avcodec_free_context(&decoder_ctx);
  }
  if (output_ctx != nullptr) {
    if (output_ctx->pb != nullptr &&
        !(output_ctx->oformat->flags & AVFMT_NOFILE)) {
      avio_closep(&output_ctx->pb);
    }
    avformat_free_context(output_ctx);
  }
  if (input_ctx != nullptr) {
    avformat_close_input(&input_ctx);
  }
  if (input_avio != nullptr) {
    if (input_avio->buffer != nullptr) {
      av_free(input_avio->buffer);
      input_avio->buffer = nullptr;
    }
    avio_context_free(&input_avio);
  }
  return success;
}
#endif

bool fallback_ffmpeg_vp9_to_h264(const std::vector<uint8_t> &vp9_ivf,
                                 const std::filesystem::path &target_mp4) {
  if (vp9_ivf.empty()) {
    return false;
  }

  if (!target_mp4.parent_path().empty()) {
    std::filesystem::create_directories(target_mp4.parent_path());
  }

#if MAICONV_HAS_LIBAV
  if (transcode_vp9_ivf_to_h264_mp4_libav(vp9_ivf, target_mp4)) {
    return true;
  }
#endif

#if defined(_WIN32)
  const bool ok = run_ffmpeg_feed_stdin(
      {L"-y", L"-loglevel", L"error", L"-f", L"ivf", L"-i", L"pipe:0", L"-an",
       L"-c:v", L"libx264", L"-pix_fmt", L"yuv420p", target_mp4.wstring()},
      vp9_ivf);
#else
  const bool ok = run_ffmpeg_feed_stdin(
      {"-y", "-loglevel", "error", "-f", "ivf", "-i", "pipe:0", "-an", "-c:v",
       "libx264", "-pix_fmt", "yuv420p", path_to_utf8(target_mp4)},
      vp9_ivf);
#endif

  return ok && file_non_empty(target_mp4);
}

bool transcode_mp4_to_vp9_ivf_bytes(const std::filesystem::path &source_mp4,
                                    std::vector<uint8_t> &target_ivf) {
  if (!file_non_empty(source_mp4)) {
    return false;
  }

  target_ivf.clear();

#if MAICONV_HAS_LIBAV
  if (transcode_mp4_to_vp9_ivf_libav(source_mp4, target_ivf) &&
      is_vp9_ivf_stream(target_ivf)) {
    return true;
  }
#endif

#if defined(_WIN32)
  const bool ok = run_ffmpeg_capture_stdout(
      {L"-y", L"-loglevel", L"error", L"-i", source_mp4.wstring(), L"-an",
       L"-c:v", L"libvpx-vp9", L"-f", L"ivf", L"pipe:1"},
      target_ivf);
#else
  const bool ok = run_ffmpeg_capture_stdout(
      {"-y", "-loglevel", "error", "-i", path_to_utf8(source_mp4), "-an",
       "-c:v", "libvpx-vp9", "-f", "ivf", "pipe:1"},
      target_ivf);
#endif
  return ok && is_vp9_ivf_stream(target_ivf);
}

bool read_binary_file(const std::filesystem::path &path,
                      std::vector<uint8_t> &out) {
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
  in.read(reinterpret_cast<char *>(out.data()),
          static_cast<std::streamsize>(out.size()));
  return static_cast<std::size_t>(in.gcount()) == out.size();
}

bool write_binary_file(const std::filesystem::path &path,
                       const std::vector<uint8_t> &data) {
  if (!path.parent_path().empty()) {
    std::filesystem::create_directories(path.parent_path());
  }
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    return false;
  }
  out.write(reinterpret_cast<const char *>(data.data()),
            static_cast<std::streamsize>(data.size()));
  out.flush();
  return out.good() && file_non_empty(path);
}

struct UsmVideoPayloadChunk {
  std::size_t payload_begin = 0;
  std::size_t payload_size = 0;
  std::vector<uint8_t> decoded_payload;
};

bool patch_template_dat_video_payloads(
    const std::filesystem::path &template_dat,
    const std::vector<uint8_t> &new_vp9_ivf,
    const std::filesystem::path &target_dat) {
  std::vector<uint8_t> bytes;
  if (!read_binary_file(template_dat, bytes) || bytes.size() < 0x20) {
    return false;
  }

  constexpr std::array<uint8_t, 4> kSfv = {
      static_cast<uint8_t>('@'), static_cast<uint8_t>('S'),
      static_cast<uint8_t>('F'), static_cast<uint8_t>('V')};
  constexpr uint64_t kUsmKey = 0x7F4551499DF55E68ULL;
  const UsmKeys keys = make_usm_keys(kUsmKey);

  struct ChannelPack {
    std::vector<UsmVideoPayloadChunk> chunks;
    std::vector<uint8_t> concatenated;
  };
  std::array<ChannelPack, 256> channels{};

  std::size_t pos = 0;
  while (pos + 0x20 <= bytes.size()) {
    const uint8_t *header = bytes.data() + static_cast<std::ptrdiff_t>(pos);
    const uint32_t chunk_size_after_header = read_u32_be(header + 4);
    const uint32_t payload_offset = header[9];
    const uint32_t padding_size = read_u16_be(header + 10);
    const uint8_t channel_number = header[12];
    const uint8_t payload_type = static_cast<uint8_t>(header[15] & 0x03U);

    const std::size_t chunk_total_size =
        0x20U + static_cast<std::size_t>(chunk_size_after_header);
    if (chunk_total_size == 0 || pos + chunk_total_size > bytes.size()) {
      return false;
    }
    if (chunk_size_after_header < payload_offset + padding_size) {
      return false;
    }

    const std::size_t payload_size = static_cast<std::size_t>(
        chunk_size_after_header - payload_offset - padding_size);
    const std::size_t extra_offset =
        payload_offset > 0x18U
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
                bytes.begin() +
                    static_cast<std::ptrdiff_t>(payload_begin + payload_size),
                payload.begin());
      std::vector<uint8_t> decoded =
          decrypt_usm_video_packet(payload, keys.video);

      UsmVideoPayloadChunk chunk;
      chunk.payload_begin = payload_begin;
      chunk.payload_size = payload_size;
      chunk.decoded_payload = std::move(decoded);

      auto &channel = channels[channel_number];
      channel.concatenated.insert(channel.concatenated.end(),
                                  chunk.decoded_payload.begin(),
                                  chunk.decoded_payload.end());
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

  auto &target_channel = channels[static_cast<std::size_t>(selected_channel)];
  std::size_t capacity = 0;
  for (const auto &chunk : target_channel.chunks) {
    capacity += chunk.payload_size;
  }
  if (new_vp9_ivf.size() > capacity) {
    return false;
  }

  std::size_t cursor = 0;
  for (auto &chunk : target_channel.chunks) {
    std::vector<uint8_t> plain(chunk.payload_size, 0U);
    const std::size_t n =
        std::min(chunk.payload_size, new_vp9_ivf.size() - cursor);
    if (n > 0) {
      std::copy(new_vp9_ivf.begin() + static_cast<std::ptrdiff_t>(cursor),
                new_vp9_ivf.begin() + static_cast<std::ptrdiff_t>(cursor + n),
                plain.begin());
      cursor += n;
    }
    std::vector<uint8_t> encrypted =
        encrypt_usm_video_packet(plain, keys.video);
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
    const std::size_t payload_size =
        std::min(kPayloadChunkSize, vp9_ivf.size() - cursor);
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

  return write_binary_file(target_dat, out);
}

bool transcode_vp9_ivf_to_h264_mp4(const std::vector<uint8_t> &vp9_ivf,
                                   const std::filesystem::path &target_mp4) {
  return fallback_ffmpeg_vp9_to_h264(vp9_ivf, target_mp4);
}

bool convert_usm_to_mp4(const std::filesystem::path &source,
                        const std::filesystem::path &target_mp4) {
  UsmVideoStream stream;
  if (!extract_usm_video_stream(source, stream)) {
    return false;
  }

  if (stream.codec != VideoCodec::kVp9Ivf) {
    return false;
  }

  return transcode_vp9_ivf_to_h264_mp4(stream.data, target_mp4);
}
} // namespace

bool convert_audio_to_mp3(const std::filesystem::path &source,
                          const std::filesystem::path &target_mp3) {
  if (!file_non_empty(source)) {
    return false;
  }

  const std::string ext = lower(source.extension().string());
  if (ext == ".mp3") {
    if (!target_mp3.parent_path().empty()) {
      std::filesystem::create_directories(target_mp3.parent_path());
    }
    std::filesystem::copy_file(
        source, target_mp3, std::filesystem::copy_options::overwrite_existing);
    return file_non_empty(target_mp3);
  }

  return decode_vgmstream_to_mp3(source, target_mp3);
}

bool convert_acb_awb_to_mp3(const std::filesystem::path &acb,
                            const std::filesystem::path &awb,
                            const std::filesystem::path &target_mp3) {
  if (!file_non_empty(acb) || !file_non_empty(awb)) {
    return false;
  }

  const auto tmp_dir = make_temp_work_dir();
  std::filesystem::path decode_acb = acb;

  const bool same_parent = acb.parent_path() == awb.parent_path();
  const bool same_stem =
      lower(acb.stem().string()) == lower(awb.stem().string());
  if (!same_parent || !same_stem) {
    decode_acb = tmp_dir / acb.filename();
    const auto staged_awb = tmp_dir / awb.filename();

    std::error_code copy_ec;
    std::filesystem::copy_file(
        acb, decode_acb, std::filesystem::copy_options::overwrite_existing,
        copy_ec);
    if (copy_ec) {
      return false;
    }

    std::filesystem::copy_file(
        awb, staged_awb, std::filesystem::copy_options::overwrite_existing,
        copy_ec);
    if (copy_ec) {
      return false;
    }

    const auto expected_awb =
        decode_acb.parent_path() / (decode_acb.stem().string() + ".awb");
    if (lower(expected_awb.filename().string()) !=
        lower(staged_awb.filename().string())) {
      std::filesystem::copy_file(
          staged_awb, expected_awb,
          std::filesystem::copy_options::overwrite_existing, copy_ec);
      if (copy_ec) {
        return false;
      }
    }
  }

  // Support maiconv's lightweight ACB stub for mp3->acb+awb roundtrip.
  std::string awb_name_from_stub;
  uint64_t awb_size_from_stub = 0;
  if (read_acb_stub_sidecar_awb_name(acb, awb_name_from_stub,
                                     awb_size_from_stub)) {
    std::error_code ec;
    const auto actual_awb_size = std::filesystem::file_size(awb, ec);
    const bool awb_name_matches =
        awb_name_from_stub.empty() ||
        lower(awb_name_from_stub) == lower(awb.filename().string());
    if (!ec && awb_name_matches && actual_awb_size == awb_size_from_stub &&
        is_mp3_like_file(awb)) {
      if (!target_mp3.parent_path().empty()) {
        std::filesystem::create_directories(target_mp3.parent_path());
      }
      std::filesystem::copy_file(
          awb, target_mp3, std::filesystem::copy_options::overwrite_existing,
          ec);
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
        ok = decode_vgmstream_to_mp3_subsong(decode_awb, *longest_subsong,
                                             target_mp3);
      }
    }
  }

  return ok;
}

bool convert_mp3_to_acb_awb(const std::filesystem::path &source_mp3,
                            const std::filesystem::path &target_acb,
                            const std::filesystem::path &target_awb) {
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
  std::filesystem::copy_file(source_mp3, target_awb,
                             std::filesystem::copy_options::overwrite_existing,
                             ec);
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
  out.write(reinterpret_cast<const char *>(kAcbStubMagic.data()),
            static_cast<std::streamsize>(kAcbStubMagic.size()));
  write_u32_le(out, 1U);
  write_u64_le(out, static_cast<uint64_t>(awb_size));
  write_u32_le(out, static_cast<uint32_t>(awb_name.size()));
  out.write(awb_name.data(), static_cast<std::streamsize>(awb_name.size()));
  out.flush();

  return out.good() && file_non_empty(target_acb);
}

bool convert_ab_to_png(const std::filesystem::path &ab_file,
                       const std::filesystem::path &png_file) {
  if (!file_non_empty(ab_file)) {
    return false;
  }
  if (write_embedded_png(ab_file, png_file)) {
    return true;
  }
  return extract_unity_texture_bundle_to_png(ab_file, png_file);
}

bool convert_dat_or_usm_to_mp4(const std::filesystem::path &source,
                               const std::filesystem::path &target_mp4) {
  if (!file_non_empty(source)) {
    return false;
  }

  if (convert_usm_to_mp4(source, target_mp4)) {
    return true;
  }

#if defined(_WIN32)
  const bool fallback_ok = run_ffmpeg_process(
      {L"-y", L"-loglevel", L"error", L"-i", source.wstring(), L"-an", L"-c:v",
       L"libx264", L"-pix_fmt", L"yuv420p", target_mp4.wstring()});
#else
  const bool fallback_ok = run_ffmpeg_process(
      {"-y", "-loglevel", "error", "-i", path_to_utf8(source), "-an", "-c:v",
       "libx264", "-pix_fmt", "yuv420p", path_to_utf8(target_mp4)});
#endif
  return fallback_ok && file_non_empty(target_mp4);
}

bool convert_mp4_to_dat(const std::filesystem::path &source_mp4,
                        const std::filesystem::path &target_dat) {
  if (!file_non_empty(source_mp4)) {
    return false;
  }

  std::vector<uint8_t> ivf_bytes;
  const bool converted =
      transcode_mp4_to_vp9_ivf_bytes(source_mp4, ivf_bytes) &&
      build_minimal_dat_from_vp9_ivf(ivf_bytes, target_dat);

  return converted;
}

bool convert_mp4_to_dat_with_template(const std::filesystem::path &source_mp4,
                                      const std::filesystem::path &template_dat,
                                      const std::filesystem::path &target_dat) {
  if (!file_non_empty(source_mp4) || !file_non_empty(template_dat)) {
    return false;
  }

  std::vector<uint8_t> ivf_bytes;
  const bool ready = transcode_mp4_to_vp9_ivf_bytes(source_mp4, ivf_bytes);
  const bool converted = ready && patch_template_dat_video_payloads(
                                      template_dat, ivf_bytes, target_dat);

  return converted;
}

} // namespace maiconv
