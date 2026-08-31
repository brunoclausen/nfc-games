#include "app.hpp"
#include "i18n.hpp"

#include <chrono>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace std::chrono_literals;

namespace {

std::filesystem::path usb_pause_path() { return nfc_config_dir() / "usb.pause"; }

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

void show_color(Acr122& r, Acr122::Led led, const char* name) {
  std::cout << "LED " << name << "\n";
  r.set_led(led);
  r.beep(150ms);
  std::this_thread::sleep_for(900ms);
}

}  // namespace

void print_help(std::ostream& out) { out << t("help_text"); }

void usage() { print_help(std::cerr); }

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

std::filesystem::path help_file() {
  std::vector<std::string> names;
  const auto code = current_lang_code();
  if (code != "da") names.push_back("HELP." + code + ".md");
  if (code != "da" && code != "en") names.push_back("HELP.en.md");
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
  std::error_code ec;
  const auto staged_dir = nfc_config_dir() / "udev";
  std::filesystem::create_directories(staged_dir, ec);
  auto helper_path = helper;
  auto rule_path = rule;
  const auto helper_copy = staged_dir / "install-udev.sh";
  const auto rule_copy = staged_dir / "99-acr122u.rules";
  ec.clear();
  std::filesystem::copy_file(helper, helper_copy,
                             std::filesystem::copy_options::overwrite_existing, ec);
  if (!ec) helper_path = helper_copy;
  ec.clear();
  std::filesystem::copy_file(rule, rule_copy,
                             std::filesystem::copy_options::overwrite_existing, ec);
  if (!ec) rule_path = rule_copy;
  std::filesystem::permissions(
      helper_path,
      std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec |
          std::filesystem::perms::others_exec,
      std::filesystem::perm_options::add, ec);
  const std::string helper_s = helper_path.string();
  const std::string rule_s = rule_path.string();
  std::vector<const char*> argv;
  if (::geteuid() != 0) argv.push_back("pkexec");
  argv.push_back(helper_s.c_str());
  argv.push_back(rule_s.c_str());
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

UsbPause::UsbPause() {
  std::error_code ec;
  std::filesystem::create_directories(nfc_config_dir(), ec);
  std::ofstream out(usb_pause_path());
  out << ::getpid() << "\n";
}

UsbPause::~UsbPause() {
  std::error_code ec;
  std::filesystem::remove(usb_pause_path(), ec);
}

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
    r.flush();
  }
}

void beep_safe(Acr122& r, std::chrono::milliseconds duration, int times) {
  try {
    r.beep(duration, times);
  } catch (const Acr122Error&) {
    r.flush();
  }
}

void signal_tag_on(Acr122& r) {
  set_led_safe(r, Acr122::Led::Yellow);
  beep_safe(r, 150ms, 2);
}

void signal_tag_off(Acr122& r) {
  set_led_safe(r, Acr122::Led::Green);
  beep_safe(r, 200ms, 1);
}

void listen_led(Acr122& r, bool tag_on) {
  set_led_safe(r, tag_on ? Acr122::Led::Yellow : Acr122::Led::Green);
}

void led_not_listening(Acr122& r) { set_led_safe(r, Acr122::Led::Red); }

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
