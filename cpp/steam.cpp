#include "steam.hpp"
#include "i18n.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <chrono>
#include <csignal>
#include <dirent.h>
#include <pwd.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

std::string home_dir() {
  if (const char* h = std::getenv("HOME"); h && *h) return h;
  if (passwd* pw = ::getpwuid(::getuid())) {
    if (pw->pw_dir && *pw->pw_dir) return pw->pw_dir;
  }
  return "/tmp";
}

bool is_tool(const SteamGame& g) {
  const std::string n = lower(g.name);
  if (n.find("proton") == 0) return true;
  if (n.find("steam linux runtime") == 0) return true;
  if (n.find("steamworks common") == 0) return true;
  if (g.appid == 228980) return true;
  return false;
}

std::optional<std::string> read_file(const fs::path& p) {
  std::ifstream in(p);
  if (!in) return std::nullopt;
  std::ostringstream os;
  os << in.rdbuf();
  return os.str();
}

std::vector<std::string> vdf_all_strings(const std::string& text, std::string_view key) {
  std::vector<std::string> out;
  const std::string pat = "\"" + std::string(key) + "\"";
  std::size_t i = 0;
  while ((i = text.find(pat, i)) != std::string::npos) {
    i += pat.size();
    auto q1 = text.find('"', i);
    if (q1 == std::string::npos) break;
    auto q2 = text.find('"', q1 + 1);
    if (q2 == std::string::npos) break;
    out.push_back(text.substr(q1 + 1, q2 - q1 - 1));
    i = q2 + 1;
  }
  return out;
}

std::optional<std::string> vdf_string(const std::string& text, std::string_view key) {
  auto all = vdf_all_strings(text, key);
  if (all.empty()) return std::nullopt;
  return all.front();
}

std::vector<fs::path> steam_roots() {
  const fs::path home = home_dir();
  std::vector<fs::path> cands = {
      home / ".steam" / "steam",
      home / ".local" / "share" / "Steam",
      home / ".var" / "app" / "com.valvesoftware.Steam" / "data" / "Steam",
  };
  if (const char* env = std::getenv("STEAM_DIR")) cands.insert(cands.begin(), fs::path(env));

  std::vector<fs::path> roots;
  std::unordered_set<std::string> seen;
  for (auto p : cands) {
    std::error_code ec;
    if (!fs::exists(p, ec)) continue;
    auto canon = fs::weakly_canonical(p, ec);
    if (ec) canon = p;
    const std::string key = canon.string();
    if (!seen.insert(key).second) continue;
    roots.push_back(canon);
  }
  return roots;
}

std::vector<fs::path> library_paths(const std::vector<fs::path>& roots) {
  std::vector<fs::path> libs;
  std::unordered_set<std::string> seen;
  auto add = [&](fs::path p) {
    std::error_code ec;
    if (!fs::exists(p, ec)) return;
    auto canon = fs::weakly_canonical(p, ec);
    if (ec) canon = p;
    if (seen.insert(canon.string()).second) libs.push_back(canon);
  };
  for (const auto& root : roots) {
    add(root);
    for (const auto& rel : {fs::path("steamapps") / "libraryfolders.vdf",
                            fs::path("config") / "libraryfolders.vdf"}) {
      auto text = read_file(root / rel);
      if (!text) continue;
      for (const auto& path : vdf_all_strings(*text, "path")) add(path);
    }
  }
  return libs;
}

std::optional<SteamGame> parse_manifest(const fs::path& acf) {
  auto text = read_file(acf);
  if (!text) return std::nullopt;
  auto name = vdf_string(*text, "name");
  auto appid_s = vdf_string(*text, "appid");
  if (!name || !appid_s || name->empty()) return std::nullopt;
  SteamGame g;
  g.kind = "steam";
  g.name = *name;
  try {
    g.appid = static_cast<std::uint32_t>(std::stoul(*appid_s));
  } catch (...) {
    return std::nullopt;
  }
  return g;
}

bool read_cstr(const std::vector<std::uint8_t>& data, std::size_t& pos, std::string& out) {
  out.clear();
  while (pos < data.size()) {
    char c = static_cast<char>(data[pos++]);
    if (c == 0) return true;
    out.push_back(c);
  }
  return false;
}

struct Bvdf {
  std::string str;
  std::uint32_t num = 0;
  bool is_int = false;
  std::vector<std::pair<std::string, Bvdf>> kids;
  const Bvdf* child(std::string_view key) const {
    for (const auto& [k, v] : kids) {
      if (lower(k) == lower(std::string(key))) return &v;
    }
    return nullptr;
  }
  std::string s(std::string_view key) const {
    if (auto* c = child(key)) return c->str;
    return {};
  }
  std::uint32_t u32(std::string_view key) const {
    if (auto* c = child(key); c && c->is_int) return c->num;
    return 0;
  }
};

bool parse_bvdf_object(const std::vector<std::uint8_t>& data, std::size_t& pos, Bvdf& obj) {
  while (pos < data.size()) {
    const std::uint8_t type = data[pos++];
    if (type == 0x08) return true;
    std::string key;
    if (!read_cstr(data, pos, key)) return false;
    Bvdf node;
    if (type == 0x00) {
      if (!parse_bvdf_object(data, pos, node)) return false;
    } else if (type == 0x01) {
      if (!read_cstr(data, pos, node.str)) return false;
    } else if (type == 0x02) {
      if (pos + 4 > data.size()) return false;
      node.is_int = true;
      node.num = static_cast<std::uint32_t>(data[pos]) |
                 (static_cast<std::uint32_t>(data[pos + 1]) << 8) |
                 (static_cast<std::uint32_t>(data[pos + 2]) << 16) |
                 (static_cast<std::uint32_t>(data[pos + 3]) << 24);
      pos += 4;
    } else if (type == 0x07) {
      if (pos + 8 > data.size()) return false;
      pos += 8;
    } else {
      return false;
    }
    obj.kids.emplace_back(std::move(key), std::move(node));
  }
  return true;
}

std::vector<SteamGame> parse_shortcuts(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return {};
  std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(in)),
                                 std::istreambuf_iterator<char>());
  if (data.size() < 2) return {};
  std::size_t pos = 0;
  Bvdf root;
  if (data[0] == 0x00) {
    pos = 1;
    std::string root_key;
    if (!read_cstr(data, pos, root_key)) return {};
    if (!parse_bvdf_object(data, pos, root)) return {};
  } else {
    return {};
  }
  std::vector<SteamGame> out;
  for (const auto& [idx, entry] : root.kids) {
    SteamGame g;
    g.kind = "shortcut";
    g.name = entry.s("appname");
    if (g.name.empty()) g.name = entry.s("AppName");
    g.exe = entry.s("exe");
    g.start_dir = entry.s("startdir");
    if (g.start_dir.empty()) g.start_dir = entry.s("StartDir");
    g.launch_options = entry.s("launchoptions");
    if (g.launch_options.empty()) g.launch_options = entry.s("LaunchOptions");
    g.appid = entry.u32("appid");
    if (g.name.empty()) continue;
    out.push_back(std::move(g));
  }
  return out;
}

std::string installdir_of(std::uint32_t appid) {
  const auto roots = steam_roots();
  for (const auto& library : library_paths(roots)) {
    auto acf = library / "steamapps" / ("appmanifest_" + std::to_string(appid) + ".acf");
    auto text = read_file(acf);
    if (!text) continue;
    if (auto dir = vdf_string(*text, "installdir")) return *dir;
  }
  return {};
}

}  // namespace

SteamLibrary SteamLibrary::scan() {
  SteamLibrary lib;
  std::unordered_set<std::uint32_t> seen_ids;
  std::unordered_set<std::string> seen_names;
  auto add = [&](SteamGame g) {
    if (g.name.empty() || is_tool(g)) return;
    if (g.appid != 0 && !seen_ids.insert(g.appid).second) return;
    const std::string nk = lower(g.name) + "|" + lower(g.exe);
    if (g.appid == 0 && !seen_names.insert(nk).second) return;
    lib.games_.push_back(std::move(g));
  };

  const auto roots = steam_roots();
  for (const auto& library : library_paths(roots)) {
    const fs::path apps = library / "steamapps";
    std::error_code ec;
    if (!fs::exists(apps, ec)) continue;
    for (const auto& ent : fs::directory_iterator(apps, ec)) {
      if (ec) break;
      const auto name = ent.path().filename().string();
      if (name.rfind("appmanifest_", 0) != 0 || ent.path().extension() != ".acf") continue;
      if (auto g = parse_manifest(ent.path())) add(*g);
    }
  }
  for (const auto& root : roots) {
    const fs::path userdata = root / "userdata";
    std::error_code ec;
    if (!fs::exists(userdata, ec)) continue;
    for (const auto& user : fs::directory_iterator(userdata, ec)) {
      if (ec || !user.is_directory()) continue;
      auto shortcuts = user.path() / "config" / "shortcuts.vdf";
      for (auto& g : parse_shortcuts(shortcuts)) add(g);
    }
  }

  std::sort(lib.games_.begin(), lib.games_.end(), [](const SteamGame& a, const SteamGame& b) {
    return lower(a.name) < lower(b.name);
  });
  return lib;
}

SteamLibrary SteamLibrary::cached_scan() {
  static SteamLibrary lib;
  static auto at = std::chrono::steady_clock::time_point{};
  const auto now = std::chrono::steady_clock::now();
  if (at.time_since_epoch().count() == 0 || now - at > std::chrono::seconds{3}) {
    lib = SteamLibrary::scan();
    at = now;
  }
  return lib;
}

std::vector<SteamGame> SteamLibrary::matches(std::string_view name_or_appid) const {
  const std::string q = lower(std::string(name_or_appid));
  std::vector<SteamGame> out;
  if (q.empty()) return out;
  for (const auto& g : games_) {
    if (std::to_string(g.appid) == q) {
      out.push_back(g);
      return out;
    }
  }
  for (const auto& g : games_) {
    if (lower(g.name) == q) out.push_back(g);
  }
  if (!out.empty()) return out;
  for (const auto& g : games_) {
    if (lower(g.name).find(q) != std::string::npos) out.push_back(g);
  }
  return out;
}

std::optional<SteamGame> SteamLibrary::find(std::string_view name_or_appid) const {
  auto hit = matches(name_or_appid);
  if (hit.size() == 1) return hit.front();
  return std::nullopt;
}

namespace {

std::string unquote(std::string s) {
  if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
    s = s.substr(1, s.size() - 2);
  }
  return s;
}

}  // namespace

std::string SteamLibrary::process_needle(const SteamGame& g) {
  const std::string opts = g.launch_options;
  auto q1 = opts.rfind('"');
  if (q1 != std::string::npos && q1 > 0) {
    auto q0 = opts.rfind('"', q1 - 1);
    if (q0 != std::string::npos && q1 - q0 > 8) {
      return opts.substr(q0 + 1, q1 - q0 - 1);
    }
  }
  return unquote(g.exe);
}

namespace {

void apply_steam_session_env() {
  DIR* proc = ::opendir("/proc");
  if (!proc) return;
  int steam_pid = 0;
  while (dirent* ent = ::readdir(proc)) {
    if (ent->d_name[0] < '1' || ent->d_name[0] > '9') continue;
    std::ifstream in("/proc/" + std::string(ent->d_name) + "/cmdline", std::ios::binary);
    if (!in) continue;
    std::string cmd((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (cmd.find("bazzite-steam") != std::string::npos ||
        cmd.find("ubuntu12_32/steam") != std::string::npos) {
      steam_pid = std::atoi(ent->d_name);
      if (cmd.find("bazzite-steam") != std::string::npos) break;
    }
  }
  ::closedir(proc);
  if (steam_pid <= 0) return;
  std::ifstream envf("/proc/" + std::to_string(steam_pid) + "/environ", std::ios::binary);
  if (!envf) return;
  std::string raw((std::istreambuf_iterator<char>(envf)), std::istreambuf_iterator<char>());
  std::string key;
  for (char c : raw) {
    if (c == '\0') {
      auto eq = key.find('=');
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

struct ProcLine {
  int pid = 0;
  std::string cmd;
};

std::string read_cmdline(int pid) {
  std::ifstream in("/proc/" + std::to_string(pid) + "/cmdline", std::ios::binary);
  if (!in) return {};
  std::string cmd((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  for (char& c : cmd) {
    if (c == '\0') c = ' ';
  }
  return cmd;
}

bool protected_proc(int pid, const std::string& cmd) {
  if (pid <= 1) return true;
  if (pid == ::getpid() || pid == ::getppid()) return true;
  if (cmd.find("steamwebhelper") != std::string::npos) return true;
  if (cmd.find("__grok_user_cmd") != std::string::npos) return true;
  if (cmd.find("/usr/bin/steam") != std::string::npos &&
      cmd.find("SteamLaunch") == std::string::npos) {
    return true;
  }
  if (cmd.find("nfc-games") != std::string::npos) return true;
  if (cmd.find("/usr/bin/nfc") != std::string::npos) return true;
  return false;
}

std::vector<ProcLine> snapshot_proc() {
  std::vector<ProcLine> out;
  DIR* proc = ::opendir("/proc");
  if (!proc) return out;
  while (dirent* ent = ::readdir(proc)) {
    if (ent->d_name[0] < '1' || ent->d_name[0] > '9') continue;
    const int pid = std::atoi(ent->d_name);
    auto cmd = read_cmdline(pid);
    if (cmd.empty()) continue;
    out.push_back({pid, std::move(cmd)});
  }
  ::closedir(proc);
  return out;
}

void collect_tree(int pid, std::vector<int>& out, std::unordered_set<int>& seen) {
  if (pid <= 1 || !seen.insert(pid).second) return;
  out.push_back(pid);
  std::ifstream in("/proc/" + std::to_string(pid) + "/task/" + std::to_string(pid) +
                   "/children");
  int child = 0;
  while (in >> child) collect_tree(child, out, seen);
}

std::uint32_t appid_from_steamlaunch(const std::string& cmd) {
  if (cmd.find("SteamLaunch") == std::string::npos) return 0;
  auto pos = cmd.find("AppId=");
  if (pos == std::string::npos) return 0;
  pos += 6;
  try {
    return static_cast<std::uint32_t>(std::stoul(cmd.substr(pos)));
  } catch (...) {
    return 0;
  }
}

}  // namespace

std::string SteamLibrary::steam_uri(const SteamGame& game) {
  const bool shortcut = game.kind == "shortcut" || (game.appid & 0x80000000u) != 0;
  if (shortcut) {
    const std::uint64_t gid =
        (static_cast<std::uint64_t>(game.appid) << 32) | 0x02000000ULL;
    return "steam://rungameid/" + std::to_string(gid);
  }
  return "steam://rungameid/" + std::to_string(game.appid);
}

void SteamLibrary::launch(const SteamGame& game) {
  if (game.appid == 0) throw std::runtime_error(t("missing_appid"));
  const std::string uri = steam_uri(game);
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
}

std::vector<RunningGame> SteamLibrary::running() {
  std::vector<RunningGame> out;
  const auto procs = snapshot_proc();
  std::unordered_set<std::uint32_t> seen;
  for (const auto& p : procs) {
    const auto appid = appid_from_steamlaunch(p.cmd);
    if (appid == 0) continue;
    if (!seen.insert(appid).second) continue;
    out.push_back({appid, p.pid});
  }

  auto lib = SteamLibrary::cached_scan();
  for (const auto& g : lib.games()) {
    if (g.exe.empty() || seen.count(g.appid)) continue;
    const std::string needle = process_needle(g);
    if (needle.size() < 8) continue;
    for (const auto& p : procs) {
      if (protected_proc(p.pid, p.cmd)) continue;
      if (p.cmd.find(needle) != std::string::npos) {
        out.push_back({g.appid, p.pid});
        seen.insert(g.appid);
        break;
      }
    }
  }
  return out;
}

bool SteamLibrary::is_running(std::uint32_t appid) {
  if (appid == 0) return false;
  const std::string marker = "AppId=" + std::to_string(appid);
  std::string needle;
  auto lib = SteamLibrary::cached_scan();
  for (const auto& g : lib.games()) {
    if (g.appid != appid) continue;
    needle = process_needle(g);
    break;
  }
  for (const auto& p : snapshot_proc()) {
    if (appid_from_steamlaunch(p.cmd) == appid) return true;
    if (p.cmd.find(marker) != std::string::npos) return true;
    if (needle.size() >= 8 && !protected_proc(p.pid, p.cmd) &&
        p.cmd.find(needle) != std::string::npos) {
      return true;
    }
  }
  return false;
}

std::vector<int> pids_for_appid(const std::vector<std::string>& needles) {
  std::vector<int> pids;
  std::unordered_set<int> seen;
  for (const auto& p : snapshot_proc()) {
    if (protected_proc(p.pid, p.cmd)) continue;
    bool hit = false;
    for (const auto& n : needles) {
      if (n.size() >= 8 && p.cmd.find(n) != std::string::npos) {
        hit = true;
        break;
      }
    }
    if (!hit) continue;
    std::vector<int> tree;
    collect_tree(p.pid, tree, seen);
    for (int pid : tree) pids.push_back(pid);
  }
  return pids;
}

int SteamLibrary::stop(std::uint32_t appid) {
  if (appid == 0) {
    auto list = running();
    int n = 0;
    for (const auto& g : list) n += stop(g.appid);
    return n;
  }

  auto lib = SteamLibrary::cached_scan();
  std::vector<std::string> needles;
  needles.push_back("AppId=" + std::to_string(appid));
  auto dir = installdir_of(appid);
  if (dir.size() >= 8) needles.push_back(dir);
  for (const auto& g : lib.games()) {
    if (g.appid != appid) continue;
    auto n = process_needle(g);
    if (n.size() >= 8) needles.push_back(n);
  }

  auto collect = [&] { return pids_for_appid(needles); };
  int n = 0;
  for (int pid : collect()) {
    if (pid == ::getpid() || pid == ::getppid()) continue;
    ::kill(pid, SIGTERM);
    ++n;
  }
  using clock = std::chrono::steady_clock;
  const auto deadline = clock::now() + std::chrono::seconds(5);
  while (clock::now() < deadline) {
    if (collect().empty()) break;
    std::this_thread::sleep_for(std::chrono::milliseconds{150});
  }
  for (int pid : collect()) {
    if (pid == ::getpid() || pid == ::getppid()) continue;
    ::kill(pid, SIGKILL);
    ++n;
  }
  std::this_thread::sleep_for(std::chrono::milliseconds{200});
  return n;
}
