#include "acr122.hpp"
#include "i18n.hpp"
#include "steam.hpp"
#include "tags.hpp"

#include <chrono>
#include <csignal>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace std::chrono_literals;

namespace {

volatile std::sig_atomic_t g_watch_run = 1;

void watch_signal(int) { g_watch_run = 0; }

}  // namespace

namespace {

void print_help(std::ostream& out) { out << t("help_text"); }

void usage() { print_help(std::cerr); }

std::vector<std::filesystem::path> share_dirs() {
  std::vector<std::filesystem::path> dirs;
  if (const char* share = std::getenv("NFC_SHARE"); share && *share) {
    dirs.emplace_back(share);
  }
  dirs.push_back(std::filesystem::current_path());
  char buf[4096];
  const ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
  if (n > 0) {
    buf[n] = 0;
    const auto exe = std::filesystem::path(buf);
    dirs.push_back(exe.parent_path());
    dirs.push_back(exe.parent_path().parent_path());
    dirs.push_back(exe.parent_path().parent_path() / "share" / "nfc-games");
    dirs.push_back(exe.parent_path().parent_path().parent_path() / "share" / "nfc-games");
  }
  return dirs;
}

std::filesystem::path help_file() {
  std::vector<std::string> names;
  const auto code = current_lang_code();
  if (code != "da") names.push_back("HELP." + code + ".md");
  names.emplace_back("HELP.md");
  for (const auto& name : names) {
    for (const auto& dir : share_dirs()) {
      const auto p = dir / name;
      std::error_code ec;
      if (std::filesystem::is_regular_file(p, ec)) return p;
    }
  }
  return {};
}

std::filesystem::path udev_file() {
  const std::string name = "99-acr122u.rules";
  for (const auto& dir : share_dirs()) {
    for (const auto& p : {dir / "udev" / name, dir / name}) {
      std::error_code ec;
      if (std::filesystem::is_regular_file(p, ec)) return p;
    }
  }
  return {};
}

std::filesystem::path udev_helper() {
  const std::string name = "install-udev.sh";
  for (const auto& dir : share_dirs()) {
    for (const auto& p : {dir / "udev" / name, dir / "packaging" / name, dir / name}) {
      std::error_code ec;
      if (std::filesystem::is_regular_file(p, ec)) return p;
    }
  }
  return {};
}

int run_udev_install(const std::filesystem::path& helper, const std::filesystem::path& rule) {
  std::vector<const char*> argv;
  if (::geteuid() != 0) argv.push_back("pkexec");
  argv.push_back(helper.c_str());
  argv.push_back(rule.c_str());
  argv.push_back(nullptr);
  const pid_t pid = ::fork();
  if (pid < 0) return 1;
  if (pid == 0) {
    ::execvp(argv[0], const_cast<char**>(argv.data()));
    ::_exit(127);
  }
  int st = 0;
  if (::waitpid(pid, &st, 0) < 0) return 1;
  if (WIFEXITED(st)) return WEXITSTATUS(st);
  return 1;
}

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

std::string join_args(int argc, char** argv, int from) {
  std::string s;
  for (int i = from; i < argc; ++i) {
    if (!s.empty()) s += ' ';
    s += argv[i];
  }
  return s;
}

std::chrono::milliseconds wait_timeout() {
  if (const char* env = std::getenv("NFC_TIMEOUT_MS")) {
    int ms = std::atoi(env);
    if (ms > 0) return std::chrono::milliseconds{ms};
  }
  return 0ms;
}

std::filesystem::path usb_pause_path() { return nfc_config_dir() / "usb.pause"; }
std::filesystem::path watch_pid_path() { return nfc_config_dir() / "watch.pid"; }

bool usb_pause_requested() {
  std::ifstream in(usb_pause_path());
  if (!in) return false;
  int pid = 0;
  in >> pid;
  if (pid <= 0) return true;
  if (::kill(pid, 0) != 0 && errno == ESRCH) {
    std::error_code ec;
    std::filesystem::remove(usb_pause_path(), ec);
    return false;
  }
  return true;
}

struct UsbPause {
  UsbPause() {
    std::error_code ec;
    std::filesystem::create_directories(nfc_config_dir(), ec);
    std::ofstream out(usb_pause_path());
    out << ::getpid() << "\n";
  }
  ~UsbPause() {
    std::error_code ec;
    std::filesystem::remove(usb_pause_path(), ec);
  }
  UsbPause(const UsbPause&) = delete;
  UsbPause& operator=(const UsbPause&) = delete;
};

Acr122 open_reader() {
  std::string last = "busy";
  for (int i = 0; i < 50; ++i) {
    try {
      return Acr122::open();
    } catch (const Acr122Error& e) {
      last = e.what();
      if (last.find("busy") == std::string::npos && last.find("Busy") == std::string::npos) {
        throw;
      }
      std::this_thread::sleep_for(200ms);
    }
  }
  throw Acr122Error(std::string(t("usb_busy")) + " (" + last + ")");
}

Acr122::Led parse_led(const std::string& name) {
  if (name == "green" || name == "gron" || name == "grøn") return Acr122::Led::Green;
  if (name == "red" || name == "rod" || name == "rød") return Acr122::Led::Red;
  if (name == "yellow" || name == "gul") return Acr122::Led::Yellow;
  if (name == "off" || name == "sluk") return Acr122::Led::Off;
  throw Acr122Error(t_join("unknown_led", name));
}

void set_led_safe(Acr122& r, Acr122::Led led) {
  try {
    r.set_led(led);
  } catch (const Acr122Error&) {
  }
}

// Rød = scanneren lytter ikke (watch kører ikke / USB-fejl).
void led_not_listening(Acr122& r) { set_led_safe(r, Acr122::Led::Red); }

void show_color(Acr122& r, Acr122::Led led, const char* name) {
  std::cout << "LED " << name << "\n";
  r.set_led(led);
  r.beep(150ms);
  std::this_thread::sleep_for(900ms);
}

void demo(Acr122& r) {
  std::cout << "firmware: " << r.firmware() << "\n";
  show_color(r, Acr122::Led::Red, t("led_red"));
  show_color(r, Acr122::Led::Green, t("led_green"));
  show_color(r, Acr122::Led::Yellow, t("led_yellow"));
  std::cout << t("led_not_listening") << "\n";
  led_not_listening(r);
}

void print_tag(const std::string& uid, const TagStore& store) {
  std::cout << "uid  " << uid << "\n";
  if (auto known = store.find_uid(uid)) {
    std::cout << t("game") << " " << known->name << "\n";
    if (known->appid) std::cout << "id   " << known->appid << "  " << known->kind << "\n";
  } else {
    std::cout << t("game_unknown") << "\n";
  }
}

std::vector<uint8_t> wait_for_tag(Acr122& reader) {
  std::cout << t("wait_tag") << "\n" << std::flush;
  return reader.wait_uid(wait_timeout());
}

int resolve_game(const std::string& query, SteamGame& out) {
  auto lib = SteamLibrary::cached_scan();
  auto hit = lib.matches(query);
  if (hit.empty()) {
    std::cerr << t("no_match") << "\"" << query << "\"\n";
    std::cerr << t("run_games") << "\n";
    return 1;
  }
  if (hit.size() > 1) {
    std::cerr << t("multi_match") << "\n";
    for (const auto& g : hit) {
      std::cerr << "  " << g.appid << "  " << g.kind << "  " << g.name << "\n";
    }
    return 2;
  }
  out = hit.front();
  return 0;
}

int cmd_watch() {
  std::signal(SIGINT, watch_signal);
  std::signal(SIGTERM, watch_signal);
  std::error_code ec;
  std::filesystem::create_directories(nfc_config_dir(), ec);
  {
    std::ofstream pidf(watch_pid_path());
    pidf << ::getpid() << "\n";
  }
  std::optional<Acr122> reader;
  try {
    reader = Acr122::open();
  } catch (const Acr122Error& e) {
    std::filesystem::remove(watch_pid_path(), ec);
    throw;
  }
  led_not_listening(*reader);
  std::cout << "watch  firmware " << reader->firmware() << "\n" << std::flush;

  std::string active_uid;
  std::uint32_t active_appid = 0;
  int present = 0;
  int absent = 0;
  int errors = 0;
  bool deaf = true;

  while (g_watch_run) {
    if (usb_pause_requested()) {
      if (reader) {
        led_not_listening(*reader);
        reader.reset();
        deaf = true;
        present = 0;
        std::cout << t("watch_paused") << "\n" << std::flush;
      }
      std::this_thread::sleep_for(200ms);
      continue;
    }
    if (!reader) {
      try {
        reader = Acr122::open();
        deaf = true;
        errors = 0;
      } catch (const Acr122Error&) {
        std::this_thread::sleep_for(250ms);
        continue;
      }
    }

    std::optional<std::vector<std::uint8_t>> uid;
    bool poll_ok = true;
    try {
      uid = reader->try_uid();
    } catch (const Acr122Error&) {
      uid.reset();
      poll_ok = false;
    }

    if (!poll_ok) {
      present = 0;
      ++errors;
      if (errors >= 3 && !deaf) {
        deaf = true;
        std::cout << t("watch_not_listening") << "\n" << std::flush;
        led_not_listening(*reader);
      }
      std::this_thread::sleep_for(250ms);
      continue;
    }
    if (deaf) {
      deaf = false;
      errors = 0;
      if (active_uid.empty() && !uid) {
        set_led_safe(*reader, Acr122::Led::Green);
        std::cout << t("watch_ready") << "\n" << std::flush;
      } else {
        set_led_safe(*reader, Acr122::Led::Yellow);
        std::cout << t("watch_listening_again") << "\n" << std::flush;
      }
    }
    errors = 0;

    if (uid) {
      absent = 0;
      ++present;
      const std::string hex = Acr122::uid_hex(*uid);
      if (present >= 2 && hex != active_uid) {
        if (active_appid != 0) {
          std::cout << t("watch_switch_stop") << active_appid << "\n" << std::flush;
          SteamLibrary::stop(active_appid);
          active_appid = 0;
        }
        auto store = TagStore::load(TagStore::default_path());
        auto known = store.find_uid(hex);
        active_uid = hex;
        if (!known || known->appid == 0) {
          std::cout << t("watch_unknown_tag") << hex << "\n" << std::flush;
          set_led_safe(*reader, Acr122::Led::Red);
        } else {
          std::cout << t("watch_tag_on") << hex << "  " << known->name << "\n" << std::flush;
          set_led_safe(*reader, Acr122::Led::Yellow);
          try {
            reader->beep(200ms);
          } catch (const Acr122Error&) {
          }
          SteamGame game;
          game.appid = known->appid;
          game.name = known->name;
          game.kind = known->kind;
          if (auto full = SteamLibrary::cached_scan().find(std::to_string(known->appid))) {
            game = *full;
          }
          for (const auto& r : SteamLibrary::running()) {
            if (r.appid != game.appid) SteamLibrary::stop(r.appid);
          }
          if (!SteamLibrary::is_running(game.appid)) {
            std::cout << t("watch_start_steam") << game.name << "  "
                      << SteamLibrary::steam_uri(game) << "\n"
                      << std::flush;
            SteamLibrary::launch(game);
          }
          active_appid = game.appid;
        }
      }
    } else {
      present = 0;
      ++absent;
      if (absent >= 3 && !active_uid.empty()) {
        std::cout << t("watch_tag_off") << "\n" << std::flush;
        if (active_appid != 0) {
          std::cout << t("watch_stopping") << active_appid << "\n" << std::flush;
          SteamLibrary::stop(active_appid);
        }
        active_uid.clear();
        active_appid = 0;
        set_led_safe(*reader, Acr122::Led::Green);
        std::cout << t("watch_ready") << "\n" << std::flush;
      }
    }
    std::this_thread::sleep_for(250ms);
  }

  if (active_appid != 0) {
    std::cout << t("watch_stopping") << active_appid << "\n" << std::flush;
    SteamLibrary::stop(active_appid);
  }
  if (reader) led_not_listening(*reader);
  std::filesystem::remove(watch_pid_path(), ec);
  std::cout << t("watch_stopped") << "\n";
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

std::string name_for_appid(std::uint32_t appid) {
  auto lib = SteamLibrary::cached_scan();
  for (const auto& g : lib.games()) {
    if (g.appid == appid) return g.name;
  }
  return std::to_string(appid);
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

int cmd_read() {
  auto store = TagStore::load(TagStore::default_path());
  UsbPause pause;
  auto reader = open_reader();
  auto uid = wait_for_tag(reader);
  print_tag(Acr122::uid_hex(uid), store);
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
  led_not_listening(reader);
  std::cout << t("saved") << game.name << "  " << hex << "\n";
  std::cout << t("file") << store.path().string() << "\n";
  return 0;
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
    std::filesystem::permissions(helper, std::filesystem::perms::owner_exec, std::filesystem::perm_options::add, ec);
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
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  i18n_init();
  try {
    for (int i = 1; i < argc; ++i) {
      const std::string a = argv[i];
      if (a == "--help" || a == "-h") {
        print_help(std::cout);
        return 0;
      }
    }
    if (argc < 2) {
      usage();
      return 2;
    }
    const std::string cmd = argv[1];
    if (cmd == "help" || cmd == "hjælp") {
      return cmd_help();
    }
    if (cmd == "list" || cmd == "ls") return cmd_list();
    if (cmd == "games" || cmd == "spil" || cmd == "steam") return cmd_games();
    if (cmd == "watch" || cmd == "run") return cmd_watch();
    if (cmd == "start" || cmd == "play" || cmd == "launch") {
      return cmd_start(join_args(argc, argv, 2));
    }
    if (cmd == "stop" || cmd == "luk") {
      return cmd_stop(join_args(argc, argv, 2));
    }
    if (cmd == "lock" || cmd == "laas" || cmd == "lås") {
      return cmd_lock();
    }
    if (cmd == "read" || cmd == "læs" || cmd == "laes") return cmd_read();
    if (cmd == "add" || cmd == "tilfoj" || cmd == "tilføj") {
      const std::string name = join_args(argc, argv, 2);
      if (name.empty()) {
        usage();
        return 2;
      }
      return cmd_add(name);
    }
    if (cmd == "remove" || cmd == "rm" || cmd == "slet") {
      return cmd_remove(join_args(argc, argv, 2));
    }
    if (cmd == "sprog" || cmd == "lang" || cmd == "language") {
      return cmd_lang(join_args(argc, argv, 2));
    }
    if (cmd == "udev") {
      return cmd_udev(join_args(argc, argv, 2));
    }

    UsbPause pause;
    auto reader = open_reader();
    if (cmd == "firmware") {
      std::cout << reader.firmware() << "\n";
    } else if (cmd == "led") {
      if (argc < 3) {
        usage();
        return 2;
      }
      reader.set_led(parse_led(argv[2]));
    } else if (cmd == "beep") {
      int ms = 200;
      if (argc >= 3) ms = std::atoi(argv[2]);
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
  } catch (const Acr122Error& e) {
    std::cerr << "nfc: " << e.what() << "\n";
    return 1;
  } catch (const std::exception& e) {
    std::cerr << "nfc: " << e.what() << "\n";
    return 1;
  }
}
