#include "plat.hpp"
#include "i18n.hpp"

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifndef _WIN32

#include <csignal>
#include <dirent.h>
#include <fcntl.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#else

#include <windows.h>
#include <shlobj.h>
#include <tlhelp32.h>

#include <io.h>
#include <stdio.h>

#endif

namespace plat {
namespace {

#ifndef _WIN32

std::string read_file(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return {};
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

void apply_steam_session_env() {
  DIR* proc = ::opendir("/proc");
  if (!proc) return;
  int steam_pid = 0;
  while (dirent* ent = ::readdir(proc)) {
    if (ent->d_name[0] < '1' || ent->d_name[0] > '9') continue;
    const std::string cmd = read_file("/proc/" + std::string(ent->d_name) + "/cmdline");
    if (cmd.find("bazzite-steam") != std::string::npos ||
        cmd.find("ubuntu12_32/steam") != std::string::npos) {
      steam_pid = std::atoi(ent->d_name);
      if (cmd.find("bazzite-steam") != std::string::npos) break;
    }
  }
  ::closedir(proc);
  if (steam_pid <= 0) return;
  const std::string raw = read_file("/proc/" + std::to_string(steam_pid) + "/environ");
  std::string key;
  for (char c : raw) {
    if (c == '\0') {
      const auto eq = key.find('=');
      if (eq != std::string::npos) {
        const std::string k = key.substr(0, eq);
        if (k == "DISPLAY" || k == "WAYLAND_DISPLAY" || k == "XDG_RUNTIME_DIR" ||
            k == "DBUS_SESSION_BUS_ADDRESS" || k == "XDG_SESSION_TYPE" ||
            k == "XAUTHORITY" || k == "XDG_SESSION_DESKTOP" || k == "XDG_CURRENT_DESKTOP") {
          ::setenv(k.c_str(), key.c_str() + eq + 1, 1);
        }
      }
      key.clear();
    } else {
      key.push_back(c);
    }
  }
}

#else

std::wstring widen(std::string_view s) {
  if (s.empty()) return {};
  const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                                      nullptr, 0);
  if (n <= 0) return {};
  std::wstring w(static_cast<std::size_t>(n), L'\0');
  ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
  return w;
}

std::string narrow(std::wstring_view w) {
  if (w.empty()) return {};
  const int n = ::WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()),
                                      nullptr, 0, nullptr, nullptr);
  if (n <= 0) return {};
  std::string s(static_cast<std::size_t>(n), '\0');
  ::WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n,
                        nullptr, nullptr);
  return s;
}

std::wstring quote_win(const std::wstring& s) {
  if (s.empty()) return L"\"\"";
  if (s.find_first_of(L" \t\"") == std::wstring::npos) return s;
  std::wstring out = L"\"";
  for (wchar_t c : s) {
    if (c == L'"') out += L'\\';
    out += c;
  }
  out += L'"';
  return out;
}

std::wstring folder_path(int csidl) {
  wchar_t buf[MAX_PATH];
  if (FAILED(::SHGetFolderPathW(nullptr, csidl, nullptr, SHGFP_TYPE_CURRENT, buf))) return {};
  return buf;
}

std::string process_cmdline(DWORD pid) {
  HANDLE proc = ::OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
  if (!proc) proc = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!proc) return {};
  std::string image;
  wchar_t path[32768];
  DWORD path_n = 32768;
  if (::QueryFullProcessImageNameW(proc, 0, path, &path_n)) image = narrow({path, path_n});

  std::string cmd;
#if defined(_M_X64) || defined(__x86_64__)
  using NtQueryInformationProcessFn = LONG(WINAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
  auto nt = reinterpret_cast<NtQueryInformationProcessFn>(
      ::GetProcAddress(::GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess"));
#pragma GCC diagnostic pop
  struct Basic {
    PVOID reserved1;
    PVOID peb;
    PVOID reserved2[2];
    ULONG_PTR unique_pid;
    PVOID reserved3;
  };
  struct UniStr {
    std::uint16_t length;
    std::uint16_t maximum;
    std::uint32_t pad;
    std::uint64_t buffer;
  };
  Basic info{};
  if (nt && nt(proc, 0, &info, sizeof(info), nullptr) >= 0 && info.peb) {
    std::uint64_t params = 0;
    SIZE_T got = 0;
    if (::ReadProcessMemory(proc, static_cast<const std::uint8_t*>(info.peb) + 0x20, &params,
                            sizeof(params), &got) &&
        params != 0) {
      UniStr us{};
      if (::ReadProcessMemory(proc, reinterpret_cast<void*>(params + 0x70), &us, sizeof(us),
                              &got) &&
          us.buffer != 0 && us.length >= 2) {
        std::wstring w(us.length / sizeof(wchar_t), L'\0');
        if (::ReadProcessMemory(proc, reinterpret_cast<void*>(us.buffer), w.data(), us.length,
                                &got)) {
          cmd = narrow(w);
        }
      }
    }
  }
#endif
  ::CloseHandle(proc);
  if (!image.empty() && cmd.find(image) == std::string::npos) {
    if (!cmd.empty()) cmd.push_back(' ');
    cmd += image;
  }
  return cmd;
}

std::unordered_map<int, HANDLE>& shell_jobs() {
  static std::unordered_map<int, HANDLE> jobs;
  return jobs;
}

bool create_process(std::wstring command, DWORD flags, bool suspended, PROCESS_INFORMATION* pi,
                    HANDLE job) {
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  std::vector<wchar_t> buf(command.begin(), command.end());
  buf.push_back(L'\0');
  DWORD create = flags;
  if (suspended) create |= CREATE_SUSPENDED;
  if (!::CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, create, nullptr, nullptr,
                        &si, pi)) {
    return false;
  }
  if (job && !::AssignProcessToJobObject(job, pi->hProcess)) {
    ::CloseHandle(job);
    job = nullptr;
  }
  if (suspended) ::ResumeThread(pi->hThread);
  if (job) shell_jobs()[static_cast<int>(pi->dwProcessId)] = job;
  ::CloseHandle(pi->hThread);
  ::CloseHandle(pi->hProcess);
  return true;
}

#endif

}  // namespace

std::filesystem::path home_dir() {
#ifndef _WIN32
  if (const char* h = std::getenv("HOME"); h && *h) return h;
  if (passwd* pw = ::getpwuid(::getuid())) {
    if (pw->pw_dir && *pw->pw_dir) return pw->pw_dir;
  }
  return "/tmp";
#else
  if (auto p = folder_path(CSIDL_PROFILE); !p.empty()) return p;
  if (const char* h = std::getenv("USERPROFILE"); h && *h) return h;
  if (const char* h = std::getenv("HOME"); h && *h) return h;
  return std::filesystem::temp_directory_path();
#endif
}

std::filesystem::path config_home() {
  if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) return xdg;
#ifndef _WIN32
  return home_dir() / ".config";
#else
  if (auto p = folder_path(CSIDL_APPDATA); !p.empty()) return p;
  return home_dir() / "AppData" / "Roaming";
#endif
}

std::filesystem::path exe_path() {
#ifndef _WIN32
  char buf[4096];
  const ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
  if (n <= 0) return {};
  buf[n] = 0;
  return buf;
#else
  wchar_t buf[32768];
  const DWORD n = ::GetModuleFileNameW(nullptr, buf, 32768);
  if (n == 0 || n >= 32768) return {};
  return std::filesystem::path(std::wstring(buf, n));
#endif
}

std::string user_locale() {
#ifdef _WIN32
  wchar_t loc[LOCALE_NAME_MAX_LENGTH];
  if (::GetUserDefaultLocaleName(loc, LOCALE_NAME_MAX_LENGTH) == 0) return {};
  return narrow(loc);
#else
  return {};
#endif
}

int current_pid() {
#ifndef _WIN32
  return static_cast<int>(::getpid());
#else
  return static_cast<int>(::GetCurrentProcessId());
#endif
}

int parent_pid() {
#ifndef _WIN32
  return static_cast<int>(::getppid());
#else
  const int self = current_pid();
  HANDLE snap = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap == INVALID_HANDLE_VALUE) return 0;
  PROCESSENTRY32W pe{};
  pe.dwSize = sizeof(pe);
  int parent = 0;
  if (::Process32FirstW(snap, &pe)) {
    do {
      if (static_cast<int>(pe.th32ProcessID) == self) {
        parent = static_cast<int>(pe.th32ParentProcessID);
        break;
      }
    } while (::Process32NextW(snap, &pe));
  }
  ::CloseHandle(snap);
  return parent;
#endif
}

bool process_alive(int pid) {
  if (pid <= 0) return false;
#ifndef _WIN32
  if (::kill(pid, 0) == 0) return true;
  return errno != ESRCH;
#else
  HANDLE proc = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                              static_cast<DWORD>(pid));
  if (!proc) return ::GetLastError() == ERROR_ACCESS_DENIED;
  DWORD code = 0;
  const BOOL ok = ::GetExitCodeProcess(proc, &code);
  ::CloseHandle(proc);
  return ok && code == STILL_ACTIVE;
#endif
}

void signal_term(int pid) {
  if (pid <= 0) return;
#ifndef _WIN32
  ::kill(pid, SIGTERM);
#else
  HANDLE proc = ::OpenProcess(PROCESS_TERMINATE, FALSE, static_cast<DWORD>(pid));
  if (!proc) return;
  ::TerminateProcess(proc, 1);
  ::CloseHandle(proc);
#endif
}

void signal_kill(int pid) {
  if (pid <= 0) return;
#ifndef _WIN32
  ::kill(pid, SIGKILL);
#else
  signal_term(pid);
#endif
}

bool stdin_is_tty() {
#ifndef _WIN32
  return ::isatty(STDIN_FILENO) != 0;
#else
  return ::_isatty(::_fileno(stdin)) != 0;
#endif
}

void console_utf8() {
#ifdef _WIN32
  ::SetConsoleOutputCP(CP_UTF8);
  ::SetConsoleCP(CP_UTF8);
#endif
}

bool is_executable(const std::filesystem::path& path) {
#ifndef _WIN32
  return ::access(path.c_str(), X_OK) == 0;
#else
  std::error_code ec;
  return std::filesystem::is_regular_file(path, ec);
#endif
}

std::vector<Proc> snapshot_processes() {
  std::vector<Proc> out;
#ifndef _WIN32
  DIR* proc = ::opendir("/proc");
  if (!proc) return out;
  while (dirent* ent = ::readdir(proc)) {
    if (ent->d_name[0] < '1' || ent->d_name[0] > '9') continue;
    Proc line;
    line.pid = std::atoi(ent->d_name);
    std::string cmd = read_file("/proc/" + std::to_string(line.pid) + "/cmdline");
    if (cmd.empty()) continue;
    for (char& c : cmd) {
      if (c == '\0') c = ' ';
    }
    line.cmd = std::move(cmd);
    const std::string stat = read_file("/proc/" + std::to_string(line.pid) + "/stat");
    const auto paren = stat.rfind(')');
    if (paren != std::string::npos && paren + 2 < stat.size()) {
      std::istringstream is(stat.substr(paren + 2));
      char state = 0;
      is >> state >> line.ppid;
    }
    out.push_back(std::move(line));
  }
  ::closedir(proc);
#else
  HANDLE snap = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap == INVALID_HANDLE_VALUE) return out;
  PROCESSENTRY32W pe{};
  pe.dwSize = sizeof(pe);
  if (::Process32FirstW(snap, &pe)) {
    do {
      Proc line;
      line.pid = static_cast<int>(pe.th32ProcessID);
      line.ppid = static_cast<int>(pe.th32ParentProcessID);
      line.cmd = process_cmdline(pe.th32ProcessID);
      if (line.cmd.empty()) line.cmd = narrow(pe.szExeFile);
      if (!line.cmd.empty()) out.push_back(std::move(line));
    } while (::Process32NextW(snap, &pe));
  }
  ::CloseHandle(snap);
#endif
  return out;
}

std::vector<int> descendant_pids(int pid) {
  std::vector<int> out;
  if (pid <= 1) return out;
  std::unordered_set<int> seen;
#ifndef _WIN32
  auto walk = [&](auto&& self, int p) -> void {
    if (p <= 1 || !seen.insert(p).second) return;
    out.push_back(p);
    std::ifstream in("/proc/" + std::to_string(p) + "/task/" + std::to_string(p) + "/children");
    int child = 0;
    while (in >> child) self(self, child);
  };
  walk(walk, pid);
#else
  std::unordered_map<int, std::vector<int>> kids;
  for (const auto& proc : snapshot_processes()) kids[proc.ppid].push_back(proc.pid);
  auto walk = [&](auto&& self, int p) -> void {
    if (p <= 1 || !seen.insert(p).second) return;
    out.push_back(p);
    auto it = kids.find(p);
    if (it == kids.end()) return;
    for (int child : it->second) self(self, child);
  };
  walk(walk, pid);
#endif
  return out;
}

int run_wait(const std::vector<std::string>& argv, bool quiet) {
  if (argv.empty() || argv[0].empty()) return 127;
#ifndef _WIN32
  std::vector<char*> raw;
  raw.reserve(argv.size() + 1);
  for (const auto& s : argv) raw.push_back(const_cast<char*>(s.c_str()));
  raw.push_back(nullptr);
  const pid_t pid = ::fork();
  if (pid < 0) return 127;
  if (pid == 0) {
    if (quiet) {
      const int fd = ::open("/dev/null", O_RDWR);
      if (fd >= 0) {
        ::dup2(fd, STDOUT_FILENO);
        ::dup2(fd, STDERR_FILENO);
        if (fd > 2) ::close(fd);
      }
    }
    ::execvp(raw[0], raw.data());
    ::_exit(127);
  }
  int st = 0;
  if (::waitpid(pid, &st, 0) < 0) return 127;
  if (WIFEXITED(st)) return WEXITSTATUS(st);
  return 1;
#else
  std::wstring command;
  for (const auto& arg : argv) {
    if (!command.empty()) command.push_back(L' ');
    command += quote_win(widen(arg));
  }
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  HANDLE nul = INVALID_HANDLE_VALUE;
  if (quiet) {
    nul = ::CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0,
                        nullptr);
    if (nul != INVALID_HANDLE_VALUE) {
      si.dwFlags = STARTF_USESTDHANDLES;
      si.hStdInput = ::GetStdHandle(STD_INPUT_HANDLE);
      si.hStdOutput = nul;
      si.hStdError = nul;
    }
  }
  std::vector<wchar_t> buf(command.begin(), command.end());
  buf.push_back(L'\0');
  PROCESS_INFORMATION pi{};
  const BOOL ok = ::CreateProcessW(nullptr, buf.data(), nullptr, nullptr, quiet ? TRUE : FALSE,
                                   CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
  if (nul != INVALID_HANDLE_VALUE) ::CloseHandle(nul);
  if (!ok) return 127;
  ::WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD code = 1;
  ::GetExitCodeProcess(pi.hProcess, &code);
  ::CloseHandle(pi.hThread);
  ::CloseHandle(pi.hProcess);
  return static_cast<int>(code);
#endif
}

int spawn_shell(const std::string& command) {
#ifndef _WIN32
  int p[2];
  if (::pipe(p) != 0) return -1;
  const pid_t outer = ::fork();
  if (outer < 0) {
    ::close(p[0]);
    ::close(p[1]);
    return -1;
  }
  if (outer > 0) {
    ::close(p[1]);
    int st = 0;
    ::waitpid(outer, &st, 0);
    pid_t inner = -1;
    ssize_t n = 0;
    do {
      const ssize_t r = ::read(p[0], &inner, sizeof(inner));
      if (r <= 0) break;
      n = r;
    } while (n != static_cast<ssize_t>(sizeof(inner)));
    ::close(p[0]);
    return inner;
  }
  ::close(p[0]);
  const pid_t inner = ::fork();
  if (inner < 0) {
    ::close(p[1]);
    ::_exit(127);
  }
  if (inner > 0) {
    const ssize_t w = ::write(p[1], &inner, sizeof(inner));
    (void)w;
    ::close(p[1]);
    ::_exit(0);
  }
  ::close(p[1]);
  ::setsid();
  const int fd = ::open("/dev/null", O_RDWR);
  if (fd >= 0) {
    ::dup2(fd, STDIN_FILENO);
    ::dup2(fd, STDOUT_FILENO);
    ::dup2(fd, STDERR_FILENO);
    if (fd > 2) ::close(fd);
  }
  ::execl("/bin/sh", "sh", "-c", command.c_str(), static_cast<char*>(nullptr));
  ::_exit(127);
#else
  const auto dir = std::filesystem::temp_directory_path() / "nfc-games";
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  const auto bat =
      dir / ("run-" + std::to_string(current_pid()) + "-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
             ".cmd");
  {
    std::ofstream out(bat, std::ios::binary | std::ios::trunc);
    if (!out) return -1;
    const unsigned char bom[2] = {0xFF, 0xFE};
    out.write(reinterpret_cast<const char*>(bom), 2);
    const std::wstring body = L"@echo off\r\n" + widen(command) + L"\r\n";
    out.write(reinterpret_cast<const char*>(body.data()),
              static_cast<std::streamsize>(body.size() * sizeof(wchar_t)));
  }
  HANDLE job = ::CreateJobObjectW(nullptr, nullptr);
  std::wstring command_line = L"cmd.exe /d /c " + quote_win(bat.wstring());
  PROCESS_INFORMATION pi{};
  const DWORD flags = CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP;
  if (!create_process(std::move(command_line), flags, true, &pi, job)) {
    if (job) ::CloseHandle(job);
    return -1;
  }
  return static_cast<int>(pi.dwProcessId);
#endif
}

void stop_shell(int pid) {
  if (pid <= 0) return;
#ifndef _WIN32
  const auto group = -pid;
  ::kill(group, SIGTERM);
  for (int i = 0; i < 33; ++i) {
    if (::kill(group, 0) != 0) break;
    std::this_thread::sleep_for(std::chrono::milliseconds{150});
  }
  if (::kill(group, 0) == 0) ::kill(group, SIGKILL);
  std::this_thread::sleep_for(std::chrono::milliseconds{200});
#else
  HANDLE job = nullptr;
  auto it = shell_jobs().find(pid);
  if (it != shell_jobs().end()) {
    job = it->second;
    shell_jobs().erase(it);
  }
  if (job) {
    ::TerminateJobObject(job, 1);
    ::CloseHandle(job);
    return;
  }
  auto tree = descendant_pids(pid);
  for (auto it_pid = tree.rbegin(); it_pid != tree.rend(); ++it_pid) signal_kill(*it_pid);
#endif
}

void spawn_hook(const std::string& command, const std::string& id, const std::string& name) {
  if (command.empty()) return;
#ifndef _WIN32
  const pid_t a = ::fork();
  if (a < 0) return;
  if (a == 0) {
    const pid_t b = ::fork();
    if (b < 0) ::_exit(1);
    if (b > 0) ::_exit(0);
    ::setsid();
    ::execl("/bin/sh", "sh", "-c", command.c_str(), "nfc", id.c_str(), name.c_str(),
            static_cast<char*>(nullptr));
    ::_exit(127);
  }
  ::waitpid(a, nullptr, 0);
#else
  ::SetEnvironmentVariableA("NFC_ID", id.c_str());
  ::SetEnvironmentVariableA("NFC_NAME", name.c_str());
  (void)spawn_shell(command);
#endif
}

int spawn_watch(const std::filesystem::path& exe) {
  if (exe.empty()) return 1;
#ifndef _WIN32
  const pid_t pid = ::fork();
  if (pid < 0) return 1;
  if (pid == 0) {
    ::setsid();
    const pid_t grand = ::fork();
    if (grand < 0) ::_exit(127);
    if (grand > 0) ::_exit(0);
    const int fd = ::open("/dev/null", O_RDWR);
    if (fd >= 0) {
      ::dup2(fd, STDIN_FILENO);
      if (fd > 2) ::close(fd);
    }
    ::setenv("NFC_SKIP_INSTALL", "1", 1);
    const auto s = exe.string();
    ::execl(s.c_str(), s.c_str(), "watch", nullptr);
    ::_exit(127);
  }
  int st = 0;
  ::waitpid(pid, &st, 0);
  return 0;
#else
  ::SetEnvironmentVariableA("NFC_SKIP_INSTALL", "1");
  std::wstring command = quote_win(exe.wstring()) + L" watch";
  PROCESS_INFORMATION pi{};
  const DWORD flags = CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP;
  if (!create_process(std::move(command), flags, false, &pi, nullptr)) return 1;
  return 0;
#endif
}

void open_uri(const std::string& uri) {
#ifndef _WIN32
  const pid_t pid = ::fork();
  if (pid < 0) throw std::runtime_error(t("steam_fork"));
  if (pid > 0) {
    int st = 0;
    ::waitpid(pid, &st, 0);
    return;
  }
  const pid_t child = ::fork();
  if (child < 0) ::_exit(127);
  if (child > 0) ::_exit(0);
  ::setsid();
  ::execlp("xdg-open", "xdg-open", uri.c_str(), static_cast<char*>(nullptr));
  ::_exit(127);
#else
  const auto w = widen(uri);
  const auto rc = reinterpret_cast<INT_PTR>(
      ::ShellExecuteW(nullptr, L"open", w.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
  if (rc <= 32) throw std::runtime_error(std::string(t("open_fail")) + uri);
#endif
}

void open_steam_uri(const std::string& uri) {
#ifndef _WIN32
  const pid_t pid = ::fork();
  if (pid < 0) throw std::runtime_error(t("steam_fork"));
  if (pid > 0) {
    int st = 0;
    ::waitpid(pid, &st, 0);
    return;
  }
  const pid_t child = ::fork();
  if (child < 0) ::_exit(127);
  if (child > 0) ::_exit(0);
  ::setsid();
  apply_steam_session_env();
  ::execlp("steam", "steam", uri.c_str(), static_cast<char*>(nullptr));
  ::_exit(127);
#else
  const auto w = widen(uri);
  const auto rc = reinterpret_cast<INT_PTR>(
      ::ShellExecuteW(nullptr, L"open", w.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
  if (rc <= 32) throw std::runtime_error(t("steam_fork"));
#endif
}

bool steam_app_running(std::uint32_t appid) {
#if !defined(_WIN32)
  (void)appid;
  return false;
#else
  if (appid == 0) return false;
  const std::wstring key = L"Software\\Valve\\Steam\\Apps\\" + std::to_wstring(appid);
  HKEY handle = nullptr;
  if (::RegOpenKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, KEY_READ, &handle) != ERROR_SUCCESS) {
    return false;
  }
  DWORD value = 0;
  DWORD size = sizeof(value);
  DWORD type = 0;
  const LONG rc =
      ::RegQueryValueExW(handle, L"Running", nullptr, &type, reinterpret_cast<BYTE*>(&value), &size);
  ::RegCloseKey(handle);
  return rc == ERROR_SUCCESS && type == REG_DWORD && value != 0;
#endif
}

std::filesystem::path steam_registry_path() {
#ifdef _WIN32
  HKEY handle = nullptr;
  if (::RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", 0, KEY_READ, &handle) !=
      ERROR_SUCCESS) {
    return {};
  }
  wchar_t buf[32768];
  DWORD size = sizeof(buf);
  DWORD type = 0;
  const LONG rc =
      ::RegQueryValueExW(handle, L"SteamPath", nullptr, &type, reinterpret_cast<BYTE*>(buf), &size);
  ::RegCloseKey(handle);
  if (rc != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) || size < sizeof(wchar_t)) {
    return {};
  }
  return std::wstring(buf);
#else
  return {};
#endif
}

}  // namespace plat
