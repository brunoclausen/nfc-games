#include "cmds.hpp"
#include "app.hpp"
#include "commands.hpp"
#include "i18n.hpp"
#include "watch.hpp"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

#ifndef NFC_VERSION
#define NFC_VERSION "dev"
#endif

using namespace std::chrono_literals;

namespace {

std::string name_for_appid(std::uint32_t appid) {
  auto lib = SteamLibrary::cached_scan();
  for (const auto& g : lib.games()) {
    if (g.appid == appid) return g.name;
  }
  return std::to_string(appid);
}

}  // namespace

int cmd_help() {
  const auto path = help_file();
  if (path.empty()) {
    usage();
    std::cerr << t("help_missing") << "\n";
    return 1;
  }
  std::ifstream in(path);
  std::cout << in.rdbuf();
  return 0;
}

int cmd_start(const std::string& query) {
  SteamGame game;
  if (query.empty()) {
    auto store = TagStore::load(TagStore::default_path());
    UsbPause pause;
    auto reader = open_reader();
    auto uid = wait_for_tag(reader);
    const std::string hex = Acr122::uid_hex(uid);
    auto known = store.find_uid(hex);
    if (!known || known->appid == 0) {
      reader.set_led(Acr122::Led::Red);
      std::cout << "uid  " << hex << "\n";
      std::cerr << t("tag_unbound") << "\n";
      return 1;
    }
    game.appid = known->appid;
    game.name = known->name;
    game.kind = known->kind;
    reader.blink(Acr122::Led::Yellow, Acr122::Led::Red, 150ms, 80ms, 1, true);
  } else {
    int rc = resolve_game(query, game);
    if (rc != 0) return rc;
  }
  if (auto full = SteamLibrary::cached_scan().find(std::to_string(game.appid))) {
    game = *full;
  }
  auto run = SteamLibrary::running();
  for (const auto& r : run) {
    if (r.appid == game.appid) {
      std::cout << t("already_running") << game.name << "  " << game.appid << "\n";
      return 0;
    }
  }
  if (!run.empty()) {
    std::cerr << t("locked") << run.front().appid << t("locked_suffix") << "\n";
    return 3;
  }
  std::cout << t("start_steam") << game.name << "  " << SteamLibrary::steam_uri(game)
            << "\n";
  SteamLibrary::launch(game);
  return 0;
}

int cmd_lock() {
  auto run = SteamLibrary::running();
  if (run.empty()) {
    std::cout << t("nothing_running_unlocked") << "\n";
    return 0;
  }
  for (const auto& r : run) {
    std::cout << t("locked_line") << r.appid << "  " << name_for_appid(r.appid) << "  pid "
              << r.pid << "\n";
  }
  return 0;
}

int cmd_stop(const std::string& query) {
  std::uint32_t appid = 0;
  std::string name;
  if (!query.empty()) {
    SteamGame game;
    int rc = resolve_game(query, game);
    if (rc != 0) return rc;
    appid = game.appid;
    name = game.name;
  } else {
    auto run = SteamLibrary::running();
    if (run.empty()) {
      std::cout << t("nothing_running") << "\n";
      return 0;
    }
    appid = run.front().appid;
    name = name_for_appid(appid);
  }
  std::cout << t("stopping") << name << "  " << appid << "\n";
  int n = SteamLibrary::stop(appid);
  bool still = false;
  for (const auto& r : SteamLibrary::running()) {
    if (r.appid == appid) still = true;
  }
  if (still) {
    std::cerr << t("still_running") << "\n";
    return 1;
  }
  if (n == 0) {
    std::cout << t("nothing_running") << "\n";
    return 0;
  }
  std::cout << t("stopped") << "\n";
  return 0;
}

int cmd_games() {
  auto lib = SteamLibrary::scan();
  if (lib.games().empty()) {
    std::cout << t("no_steam_games") << "\n";
    return 0;
  }
  for (const auto& g : lib.games()) {
    std::cout << g.appid << "  " << g.kind << "  " << g.name << "\n";
  }
  std::cout << lib.games().size() << t("games_count_suffix") << "\n";
  return 0;
}

int cmd_list() {
  auto store = TagStore::load(TagStore::default_path());
  if (store.all().empty()) {
    std::cout << t("no_tags") << store.path().string() << "\n";
    return 0;
  }
  for (const auto& t : store.all()) {
    std::cout << t.uid << "  " << t.kind << "  " << t.appid << "  " << t.name << "\n";
  }
  return 0;
}

namespace {

void print_ndef(Acr122& reader) {
  try {
    if (auto text = reader.read_ndef_text()) {
      std::cout << t("ndef_text") << *text << "\n";
    } else {
      std::cout << t("ndef_empty") << "\n";
    }
  } catch (const Acr122Error&) {
    std::cout << t("ndef_empty") << "\n";
  }
}

int write_ndef_game(Acr122& reader, const std::string& name) {
  try {
    reader.write_ndef_text(name);
    std::cout << t("ndef_written") << name << "\n";
    std::cout << t("ndef_ok") << "\n";
    return 0;
  } catch (const Acr122Error& e) {
    std::cerr << t("ndef_write_fail") << e.what() << "\n";
    return 1;
  }
}

}  // namespace

namespace {

const char* g_hex = "0123456789ABCDEF";

std::string trim_arg(std::string s) {
  auto is_sp = [](unsigned char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
  while (!s.empty() && is_sp(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
  while (!s.empty() && is_sp(static_cast<unsigned char>(s.back()))) s.pop_back();
  return s;
}

std::string hex_byte(uint8_t b) {
  std::string s(2, '0');
  s[0] = g_hex[b >> 4];
  s[1] = g_hex[b & 0x0F];
  return s;
}

std::string hex_bytes(const std::vector<uint8_t>& v, size_t max) {
  std::string s;
  const size_t n = std::min(max, v.size());
  for (size_t i = 0; i < n; ++i) {
    s.push_back(g_hex[v[i] >> 4]);
    s.push_back(g_hex[v[i] & 0x0F]);
  }
  return s;
}

std::vector<uint8_t> unhex_bytes(const std::string& hx) {
  std::vector<uint8_t> out;
  auto val = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  for (size_t i = 0; i + 1 < hx.size(); i += 2) {
    const int hi = val(hx[i]);
    const int lo = val(hx[i + 1]);
    if (hi < 0 || lo < 0) break;
    out.push_back(static_cast<uint8_t>((hi << 4) | lo));
  }
  return out;
}

int cmd_tag_backup(const std::string& file_arg) {
  UsbPause pause;
  auto reader = open_reader();
  auto uid = wait_for_tag(reader);
  const std::string uidhex = Acr122::uid_hex(uid);

  std::vector<std::vector<uint8_t>> pages;
  pages.reserve(232);
  for (uint8_t p = 0; p < 232; ++p) {
    try {
      auto chunk = reader.read_binary(p, 4);
      if (chunk.empty()) break;
      pages.push_back(std::move(chunk));
    } catch (const Acr122Error&) {
      break;
    }
  }

  std::filesystem::path path = file_arg;
  if (path.empty()) {
    auto dir = nfc_config_dir() / "tag-backups";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const auto ts = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch())
                        .count();
    path = dir / (uidhex + "-" + std::to_string(ts) + ".hex");
  } else if (!path.parent_path().empty()) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
  }

  std::ofstream out(path);
  if (!out) {
    std::cerr << t("tag_restore_open") << path << "\n";
    return 1;
  }
  out << "UID " << uidhex << "\n";
  if (auto ver = reader.ntag_version(); ver) out << "VERSION " << hex_bytes(*ver, 8) << "\n";
  for (size_t p = 0; p < pages.size(); ++p) {
    out << hex_byte(static_cast<uint8_t>(p)) << " " << hex_bytes(pages[p], 4) << "\n";
  }
  out.close();

  std::cout << t("tag_backup_uid") << uidhex << "\n";
  std::cout << t("tag_backup_count") << pages.size() << t("tag_backup_suffix") << path << "\n";
  led_not_listening(reader);
  return 0;
}

int cmd_tag_restore(const std::string& file_arg) {
  if (file_arg.empty()) {
    std::cerr << t("tag_restore_usage") << "\n";
    return 2;
  }
  std::ifstream in(file_arg);
  if (!in) {
    std::cerr << t("tag_restore_open") << file_arg << "\n";
    return 1;
  }

  std::vector<std::vector<uint8_t>> pages(232);
  std::string want_uid;
  std::string line;
  while (std::getline(in, line)) {
    const auto sp = line.find_first_of(" \t");
    if (sp == std::string::npos) continue;
    const std::string key = line.substr(0, sp);
    const std::string val = trim_arg(line.substr(sp));
    if (key == "UID") { want_uid = val; continue; }
    if (key == "VERSION") continue;
    long p = strtol(key.c_str(), nullptr, 16);
    if (p < 0 || p > 231) continue;
    auto bytes = unhex_bytes(val);
    if (bytes.size() != 4) continue;
    pages[static_cast<size_t>(p)] = std::move(bytes);
  }

  UsbPause pause;
  auto reader = open_reader();
  auto uid = wait_for_tag(reader);
  const std::string uidhex = Acr122::uid_hex(uid);
  std::cout << t("tag_restore_uid") << uidhex << "\n";
  if (!want_uid.empty() && want_uid != uidhex) {
    std::cerr << t("tag_restore_mismatch") << want_uid << t("tag_restore_mismatch_suffix") << "\n";
    return 2;
  }

  int ok = 0, fail = 0;
  for (size_t p = 0; p < pages.size(); ++p) {
    if (p <= 2) continue;  // UID pages: readonly
    if (pages[p].size() != 4) continue;
    bool zero = true;
    for (uint8_t b : pages[p]) {
      if (b != 0) zero = false;
    }
    if (zero) continue;  // never-written pages
    try {
      reader.write_page(static_cast<uint8_t>(p), pages[p].data());
      ++ok;
    } catch (const Acr122Error&) {
      ++fail;
    }
  }
  std::cout << t("tag_restore_written") << ok << t("tag_restore_and") << fail
            << t("tag_restore_fail_suffix") << "\n";
  print_ndef(reader);
  led_not_listening(reader);
  return 0;
}

}  // namespace

int cmd_read() {
  auto store = TagStore::load(TagStore::default_path());
  UsbPause pause;
  auto reader = open_reader();
  auto uid = wait_for_tag(reader);
  print_tag(Acr122::uid_hex(uid), store);
  print_ndef(reader);
  led_not_listening(reader);
  return 0;
}

int cmd_add(const std::string& query) {
  SteamGame game;
  int rc = resolve_game(query, game);
  if (rc != 0) return rc;
  std::cout << t("game") << " " << game.name << "  " << game.appid << "  " << game.kind << "\n";
  auto store = TagStore::load(TagStore::default_path());
  UsbPause pause;
  auto reader = open_reader();
  auto uid = wait_for_tag(reader);
  const std::string hex = Acr122::uid_hex(uid);
  store.upsert(Tag{hex, game.name, game.appid, game.kind});
  (void)write_ndef_game(reader, game.name);
  led_not_listening(reader);
  std::cout << t("saved") << game.name << "  " << hex << "\n";
  std::cout << t("file") << store.path().string() << "\n";
  return 0;
}

int cmd_write() {
  auto store = TagStore::load(TagStore::default_path());
  UsbPause pause;
  auto reader = open_reader();
  auto uid = wait_for_tag(reader);
  const std::string hex = Acr122::uid_hex(uid);
  print_tag(hex, store);
  auto known = store.find_uid(hex);
  if (!known || known->name.empty()) {
    reader.set_led(Acr122::Led::Red);
    std::cerr << t("ndef_unbound") << "\n";
    return 1;
  }
  const int rc = write_ndef_game(reader, known->name);
  led_not_listening(reader);
  return rc;
}

int cmd_remove(const std::string& key) {
  auto store = TagStore::load(TagStore::default_path());
  std::string target = key;
  if (target.empty()) {
    UsbPause pause;
    auto reader = open_reader();
    auto uid = wait_for_tag(reader);
    target = Acr122::uid_hex(uid);
    auto known = store.find_uid(target);
    if (!store.remove(target)) {
      reader.set_led(Acr122::Led::Red);
      std::cout << "uid  " << target << "\n";
      std::cout << t("not_saved") << "\n";
      return 1;
    }
    reader.blink(Acr122::Led::Red, Acr122::Led::Red, 150ms, 80ms, 2, true);
    std::cout << t("removed") << (known ? known->name : target) << "  " << target << "\n";
    return 0;
  }
  auto known = store.find(target);
  if (!store.remove(target)) {
    std::cerr << t("not_found") << target << "\n";
    return 1;
  }
  std::cout << t("removed") << (known ? known->name : target);
  if (known) std::cout << "  " << known->uid;
  std::cout << "\n";
  return 0;
}

int cmd_udev(const std::string& arg) {
  const auto path = udev_file();
  if (path.empty()) {
    std::cerr << t("udev_missing") << "\n";
    return 1;
  }
  if (arg == "install" || arg == "installer") {
    const auto helper = udev_helper();
    if (helper.empty()) {
      std::cerr << t("udev_helper_missing") << "\n";
      return 1;
    }
    std::error_code ec;
    std::filesystem::permissions(helper, std::filesystem::perms::owner_exec,
                                 std::filesystem::perm_options::add, ec);
    const int rc = run_udev_install(helper, path);
    if (rc != 0) {
      std::cerr << t("udev_fail") << "\n";
      return rc;
    }
    std::cout << t("udev_ok") << "\n";
    return 0;
  }
  std::cout << t("udev_install") << "\n";
  std::cout << "  nfc udev install\n";
  std::cout << "  sudo cp \"" << path.string() << "\" /etc/udev/rules.d/\n";
  std::cout << "  sudo udevadm control --reload\n";
  std::cout << "  sudo udevadm trigger --subsystem-match=usb --attr-match=idVendor=072f --attr-match=idProduct=2200\n";
  std::cout << t("udev_replug") << "\n\n";
  std::ifstream in(path);
  std::cout << in.rdbuf();
  return 0;
}

namespace {

int run_cmd(std::vector<const char*> argv, bool quiet) {
  argv.push_back(nullptr);
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
    ::execvp(argv[0], const_cast<char**>(argv.data()));
    ::_exit(127);
  }
  int st = 0;
  if (::waitpid(pid, &st, 0) < 0) return 127;
  if (WIFEXITED(st)) return WEXITSTATUS(st);
  return 1;
}

int systemd_user(const char* action, bool quiet) {
  return run_cmd({"systemctl", "--user", action, "nfc-games.service"}, quiet);
}

bool systemd_unit_present() {
  std::filesystem::path unit;
  if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) {
    unit = std::filesystem::path(xdg) / "systemd" / "user" / "nfc-games.service";
  } else if (const char* home = std::getenv("HOME"); home && *home) {
    unit = std::filesystem::path(home) / ".config" / "systemd" / "user" / "nfc-games.service";
  }
  std::error_code ec;
  return !unit.empty() && std::filesystem::is_regular_file(unit, ec);
}

int live_watch_pid() {
  std::ifstream in(watch_pid_path());
  int pid = 0;
  if (in >> pid && pid > 0 && pid != ::getpid() && ::kill(pid, 0) == 0) return pid;
  return 0;
}

int wait_pid_gone(int pid, int ms) {
  using namespace std::chrono_literals;
  for (int waited = 0; waited < ms; waited += 100) {
    if (::kill(pid, 0) != 0 && errno == ESRCH) return 0;
    std::this_thread::sleep_for(100ms);
  }
  return (::kill(pid, 0) == 0) ? 1 : 0;
}

int stop_watch_pid(int pid) {
  if (pid <= 0) return 0;
  ::kill(pid, SIGTERM);
  if (wait_pid_gone(pid, 4000) == 0) return 0;
  ::kill(pid, SIGKILL);
  return wait_pid_gone(pid, 1000);
}

std::filesystem::path watch_binary() {
  std::error_code ec;
  if (const char* app = std::getenv("APPIMAGE"); app && *app) {
    const std::filesystem::path p(app);
    if (std::filesystem::is_regular_file(p, ec) && ::access(app, X_OK) == 0) return p;
  }
  if (const char* home = std::getenv("HOME"); home && *home) {
    const auto p = std::filesystem::path(home) / "Applications" / "nfc-games-x86_64.AppImage";
    if (std::filesystem::is_regular_file(p, ec) && ::access(p.c_str(), X_OK) == 0) return p;
  }
  char buf[4096];
  const ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
  if (n > 0) {
    buf[n] = 0;
    return buf;
  }
  return {};
}

int spawn_watch() {
  const auto bin = watch_binary();
  if (bin.empty()) return 1;
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
    const auto s = bin.string();
    ::execl(s.c_str(), s.c_str(), "watch", nullptr);
    ::_exit(127);
  }
  int st = 0;
  ::waitpid(pid, &st, 0);
  return 0;
}

int wait_new_watch_pid(int old, int ms) {
  using namespace std::chrono_literals;
  for (int waited = 0; waited < ms; waited += 100) {
    const int pid = live_watch_pid();
    if (pid > 0 && pid != old) return pid;
    std::this_thread::sleep_for(100ms);
  }
  return live_watch_pid();
}

}  // namespace

int cmd_start_watch() {
  if (const int pid = live_watch_pid()) {
    std::cout << t("watch_running") << pid << ")\n";
    return 0;
  }
  std::cout << t("watch_starting") << "\n" << std::flush;
  if (systemd_unit_present()) {
    systemd_user("daemon-reload", true);
    if (systemd_user("start", true) == 0) {
      using namespace std::chrono_literals;
      for (int i = 0; i < 40; ++i) {
        if (systemd_user("is-active", true) == 0) break;
        std::this_thread::sleep_for(100ms);
      }
      if (systemd_user("is-active", true) == 0) {
        const int pid = wait_new_watch_pid(0, 4000);
        std::cout << t("watch_started") << (pid > 0 ? pid : 0) << ")\n";
        return 0;
      }
    }
  }
  if (spawn_watch() != 0) {
    std::cerr << t("restart_fail") << "\n";
    return 1;
  }
  const int pid = wait_new_watch_pid(0, 4000);
  if (pid <= 0) {
    std::cerr << t("restart_fail") << "\n";
    return 1;
  }
  std::cout << t("watch_started") << pid << ")\n";
  return 0;
}

int cmd_restart() {
  std::cout << t("restarting") << "\n" << std::flush;
  const int old = live_watch_pid();
  if (systemd_unit_present()) {
    systemd_user("daemon-reload", true);
    int rc = systemd_user("restart", true);
    if (rc != 0) rc = systemd_user("start", true);
    if (rc == 0) {
      using namespace std::chrono_literals;
      for (int i = 0; i < 40; ++i) {
        if (systemd_user("is-active", true) == 0) break;
        std::this_thread::sleep_for(100ms);
      }
      if (systemd_user("is-active", true) == 0) {
        const int pid = wait_new_watch_pid(old, 4000);
        std::cout << t("restart_ok") << (pid > 0 ? pid : 0) << ")\n";
        return 0;
      }
    }
    std::cerr << t("restart_systemd_fail") << "\n";
  }
  if (old) stop_watch_pid(old);
  if (spawn_watch() != 0) {
    std::cerr << t("restart_fail") << "\n";
    return 1;
  }
  const int pid = wait_new_watch_pid(old, 4000);
  if (pid <= 0) {
    std::cerr << t("restart_fail") << "\n";
    return 1;
  }
  std::cout << t("restart_ok") << pid << ")\n";
  return 0;
}

int cmd_lang(const std::string& want) {
  if (want.empty()) {
    const auto cur = current_lang_code();
    for (const auto& lang : languages()) {
      std::cout << lang.code << "  " << lang.name;
      if (cur == lang.code) std::cout << "  (" << t("lang_active") << ")";
      std::cout << "\n";
    }
    return 0;
  }
  if (!set_lang(want)) {
    std::cerr << t("lang_unknown") << "\n";
    for (const auto& lang : languages()) {
      std::cerr << "  " << lang.code << "  " << lang.name << "\n";
    }
    return 2;
  }
  std::cout << t("lang_set") << current_lang_name() << " (" << current_lang_code() << ")\n";
  std::cout << t("lang_saved") << lang_config_path().string() << "\n";
  std::cout << t("restart_hint") << "\n";
  return 0;
}

int nfc_run(const std::string& cmd, const std::string& arg) {
  if (cmd == "help" || cmd == "hjælp") return cmd_help();
  if (cmd == "version") {
    std::cout << "nfc-games " << NFC_VERSION << "\n";
    return 0;
  }
  if (cmd == "list" || cmd == "ls") return cmd_list();
  if (cmd == "games" || cmd == "spil" || cmd == "steam") return cmd_games();
  if (cmd == "restart" || cmd == "genstart") return cmd_restart();
  if (cmd == "startwatch" || cmd == "lyt") return cmd_start_watch();
  if (cmd == "watch" || cmd == "run") {
    if (arg == "restart" || arg == "genstart") return cmd_restart();
    nfc_watch_arm();
    return cmd_watch();
  }
  if (cmd == "start" || cmd == "play" || cmd == "launch") return cmd_start(arg);
  if (cmd == "stop" || cmd == "luk") return cmd_stop(arg);
  if (cmd == "lock" || cmd == "laas" || cmd == "lås") return cmd_lock();
  if (cmd == "read" || cmd == "læs" || cmd == "laes") return cmd_read();
  if (cmd == "write" || cmd == "skriv") return cmd_write();
  if (cmd == "add" || cmd == "tilfoj" || cmd == "tilføj") {
    if (arg.empty()) {
      usage();
      return 2;
    }
    return cmd_add(arg);
  }
  if (cmd == "remove" || cmd == "rm" || cmd == "slet") return cmd_remove(arg);
  if (cmd == "tag" || cmd == "brik") {
    const auto sp = arg.find(' ');
    const std::string sub = sp == std::string::npos ? arg : arg.substr(0, sp);
    const std::string rest = sp == std::string::npos ? "" : trim_arg(arg.substr(sp + 1));
    if (sub == "backup" || sub == "sikkerhedskopi" || sub == "dump") return cmd_tag_backup(rest);
    if (sub == "restore" || sub == "gendan" || sub == "genopret") return cmd_tag_restore(rest);
    std::cerr << t("tag_usage") << "\n";
    return 2;
  }
  if (cmd == "sprog" || cmd == "lang" || cmd == "language") return cmd_lang(arg);
  if (cmd == "udev") return cmd_udev(arg);
  if (cmd == "install" || cmd == "installer") return cmd_udev("install");

  UsbPause pause;
  auto reader = open_reader();
  if (cmd == "firmware") {
    std::cout << reader.firmware() << "\n";
  } else if (cmd == "led") {
    if (arg.empty()) {
      usage();
      return 2;
    }
    reader.set_led(parse_led(arg));
  } else if (cmd == "beep") {
    int ms = 200;
    if (!arg.empty()) ms = std::atoi(arg.c_str());
    if (ms < 100) ms = 100;
    reader.beep(std::chrono::milliseconds{ms});
  } else if (cmd == "demo" || cmd == "colors" || cmd == "farver") {
    demo(reader);
  } else {
    usage();
    return 2;
  }
  if (cmd != "led") led_not_listening(reader);
  return 0;
}
