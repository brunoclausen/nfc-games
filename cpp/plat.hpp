#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace plat {

struct Proc {
  int pid = 0;
  int ppid = 0;
  std::string cmd;
};

std::filesystem::path home_dir();
// XDG_CONFIG_HOME, ~/.config, or %APPDATA%.
std::filesystem::path config_home();
std::filesystem::path exe_path();
// Windows UI locale such as "da-DK". Empty on Linux.
std::string user_locale();

int current_pid();
int parent_pid();
bool process_alive(int pid);
void signal_term(int pid);
void signal_kill(int pid);
bool stdin_is_tty();
void console_utf8();
bool is_executable(const std::filesystem::path& path);

std::vector<Proc> snapshot_processes();
// The pid itself, then its descendants. Empty when pid <= 1.
std::vector<int> descendant_pids(int pid);

// Wait until argv finishes. 127 if it could not be started.
int run_wait(const std::vector<std::string>& argv, bool quiet);

// Detached shell (sh -c / cmd /c). Returns the leader pid, or -1.
int spawn_shell(const std::string& command);
void stop_shell(int pid);

// Linux passes id and name as $1 and $2. Windows sets NFC_ID and NFC_NAME.
void spawn_hook(const std::string& command, const std::string& id, const std::string& name);

// Detached `<exe> watch`. 0 when the spawn was set up.
int spawn_watch(const std::filesystem::path& exe);

// Throws std::runtime_error when the launch cannot be started.
void open_uri(const std::string& uri);
void open_steam_uri(const std::string& uri);

bool steam_app_running(std::uint32_t appid);
std::filesystem::path steam_registry_path();

}  // namespace plat
