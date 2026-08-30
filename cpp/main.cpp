#include "acr122.hpp"
#include "steam.hpp"
#include "tags.hpp"

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace std::chrono_literals;

namespace {

volatile std::sig_atomic_t g_watch_run = 1;

void watch_signal(int) { g_watch_run = 0; }

}  // namespace

namespace {

void print_help(std::ostream& out) {
  out <<
      "nfc — ACR122U NFC-tags og Steam-spil\n"
      "\n"
      "Brug:\n"
      "  nfc --help\n"
      "  nfc -h\n"
      "  nfc <kommando> [argumenter]\n"
      "\n"
      "Tags og spil:\n"
      "  nfc games                   auto-scan Steam-spil og shortcuts\n"
      "  nfc watch                   tag på = gul + beep + start; tag af = stop + grøn\n"
      "  nfc start <spil|appid>      start spil via Steam (låst mens det kører)\n"
      "  nfc start                   læs tag og start bundet spil\n"
      "  nfc stop [spil|appid]       stop kørende spil\n"
      "  nfc lock                    vis hvilket spil der er låst/kører\n"
      "  nfc add <spil|appid>        find spil, læs tag, bind dem\n"
      "  nfc read                    læs tag (UID + bundet spil)\n"
      "  nfc list                    vis gemte tags (tags.conf)\n"
      "  nfc remove <spil|uid>       fjern gemt tag\n"
      "  nfc remove                  læs tag og fjern det hvis det er gemt\n"
      "\n"
      "Læser:\n"
      "  nfc farver                  test rød/grøn/gul LED + beep\n"
      "  nfc led green|red|yellow|off\n"
      "  nfc beep [ms]               bip (standard 200)\n"
      "  nfc firmware                vis ACR122U-firmware\n"
      "\n"
      "Hjælp:\n"
      "  nfc --help, nfc -h          denne tekst\n"
      "  nfc help                    vis HELP.md\n"
      "\n"
      "Filer:\n"
      "  tags.conf                   gemte tags i den mappe, du kører fra\n"
      "  HELP.md                     ~/nfc-games/HELP.md\n"
      "\n"
      "Eksempel:\n"
      "  nfc games\n"
      "  nfc add \"BloodRayne 2\"\n"
      "  nfc watch\n"
      "  nfc start \"BloodRayne 2\"\n"
      "  nfc lock\n"
      "  nfc stop\n"
      "  nfc add 3640596865\n"
      "  nfc read\n";
}

void usage() { print_help(std::cerr); }

std::filesystem::path help_file() {
  std::vector<std::filesystem::path> cands;
  cands.push_back(std::filesystem::current_path() / "HELP.md");
  char buf[4096];
  const ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
  if (n > 0) {
    buf[n] = 0;
    const auto exe = std::filesystem::path(buf);
    cands.push_back(exe.parent_path() / "HELP.md");
    cands.push_back(exe.parent_path().parent_path() / "HELP.md");
  }
  for (const auto& p : cands) {
    std::error_code ec;
    if (std::filesystem::is_regular_file(p, ec)) return p;
  }
  return {};
}

int cmd_help() {
  const auto path = help_file();
  if (path.empty()) {
    usage();
    std::cerr << "help-fil: HELP.md (ikke fundet)\n";
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

Acr122::Led parse_led(const std::string& name) {
  if (name == "green" || name == "gron" || name == "grøn") return Acr122::Led::Green;
  if (name == "red" || name == "rod" || name == "rød") return Acr122::Led::Red;
  if (name == "yellow" || name == "gul") return Acr122::Led::Yellow;
  if (name == "off" || name == "sluk") return Acr122::Led::Off;
  throw Acr122Error("ukendt LED: " + name);
}

void show_color(Acr122& r, Acr122::Led led, const char* name) {
  std::cout << "LED " << name << "\n";
  r.set_led(led);
  r.beep(150ms);
  std::this_thread::sleep_for(900ms);
}

void demo(Acr122& r) {
  std::cout << "firmware: " << r.firmware() << "\n";
  show_color(r, Acr122::Led::Red, "rød");
  show_color(r, Acr122::Led::Green, "grøn");
  show_color(r, Acr122::Led::Yellow, "gul (rød+grøn)");
  std::cout << "LED grøn (klar)\n";
  r.set_led(Acr122::Led::Green);
}

void print_tag(const std::string& uid, const TagStore& store) {
  std::cout << "uid  " << uid << "\n";
  if (auto known = store.find_uid(uid)) {
    std::cout << "spil " << known->name << "\n";
    if (known->appid) std::cout << "id   " << known->appid << "  " << known->kind << "\n";
  } else {
    std::cout << "spil (ukendt)\n";
  }
}

std::vector<uint8_t> wait_for_tag(Acr122& reader) {
  std::cout << "læg et tag på læseren...\n" << std::flush;
  return reader.wait_uid(wait_timeout());
}

int resolve_game(const std::string& query, SteamGame& out) {
  auto lib = SteamLibrary::scan();
  auto hit = lib.matches(query);
  if (hit.empty()) {
    std::cerr << "nfc: intet Steam-spil matcher \"" << query << "\"\n";
    std::cerr << "kør: nfc games\n";
    return 1;
  }
  if (hit.size() > 1) {
    std::cerr << "nfc: flere spil matcher, brug appid:\n";
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
  auto reader = Acr122::open();
  reader.set_led(Acr122::Led::Green);
  std::cout << "watch  firmware " << reader.firmware() << "\n";
  std::cout << "watch  tag på = gul + beep + start, tag af = stop + grøn\n"
            << std::flush;

  std::string active_uid;
  std::uint32_t active_appid = 0;
  int present = 0;
  int absent = 0;

  while (g_watch_run) {
    std::optional<std::vector<std::uint8_t>> uid;
    try {
      uid = reader.try_uid();
    } catch (const Acr122Error&) {
      uid.reset();
    }

    if (uid) {
      absent = 0;
      ++present;
      const std::string hex = Acr122::uid_hex(*uid);
      if (present >= 2 && hex != active_uid) {
        if (active_appid != 0) {
          std::cout << "watch  skifter tag, stopper " << active_appid << "\n" << std::flush;
          SteamLibrary::stop(active_appid);
          active_appid = 0;
        }
        auto store = TagStore::load(TagStore::default_path());
        auto known = store.find_uid(hex);
        active_uid = hex;
        if (!known || known->appid == 0) {
          std::cout << "watch  ukendt tag " << hex << "\n" << std::flush;
          try {
            reader.set_led(Acr122::Led::Red);
          } catch (const Acr122Error&) {
          }
        } else {
          std::cout << "watch  tag på  " << hex << "  " << known->name << "\n" << std::flush;
          try {
            reader.set_led(Acr122::Led::Yellow);
            reader.beep(200ms);
          } catch (const Acr122Error&) {
          }
          SteamGame game;
          game.appid = known->appid;
          game.name = known->name;
          game.kind = known->kind;
          bool already = false;
          for (const auto& r : SteamLibrary::running()) {
            if (r.appid == game.appid) already = true;
          }
          if (!already) {
            std::cout << "watch  starter " << game.name << "  " << game.appid << "\n"
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
        std::cout << "watch  tag af\n" << std::flush;
        if (active_appid != 0) {
          std::cout << "watch  stopper " << active_appid << "\n" << std::flush;
          SteamLibrary::stop(active_appid);
        }
        active_uid.clear();
        active_appid = 0;
        try {
          reader.set_led(Acr122::Led::Green);
        } catch (const Acr122Error&) {
        }
      }
    }
    std::this_thread::sleep_for(250ms);
  }

  try {
    reader.set_led(Acr122::Led::Green);
  } catch (const Acr122Error&) {
  }
  std::cout << "watch  stoppet\n";
  return 0;
}

int cmd_start(const std::string& query) {
  SteamGame game;
  if (query.empty()) {
    auto store = TagStore::load(TagStore::default_path());
    auto reader = Acr122::open();
    auto uid = wait_for_tag(reader);
    const std::string hex = Acr122::uid_hex(uid);
    auto known = store.find_uid(hex);
    if (!known || known->appid == 0) {
      reader.set_led(Acr122::Led::Red);
      std::cout << "uid  " << hex << "\n";
      std::cerr << "nfc: tagget er ikke bundet til et spil\n";
      return 1;
    }
    game.appid = known->appid;
    game.name = known->name;
    game.kind = known->kind;
    reader.blink(Acr122::Led::Yellow, Acr122::Led::Green, 150ms, 80ms, 1, true);
  } else {
    int rc = resolve_game(query, game);
    if (rc != 0) return rc;
  }
  auto run = SteamLibrary::running();
  for (const auto& r : run) {
    if (r.appid == game.appid) {
      std::cout << "kører allerede " << game.name << "  " << game.appid << "\n";
      return 0;
    }
  }
  if (!run.empty()) {
    std::cerr << "nfc: spil er låst (appid " << run.front().appid
              << " kører). nfc stop først\n";
    return 3;
  }
  std::cout << "starter " << game.name << "  " << game.appid << "  " << game.kind << "\n";
  SteamLibrary::launch(game);
  return 0;
}

std::string name_for_appid(std::uint32_t appid) {
  auto lib = SteamLibrary::scan();
  for (const auto& g : lib.games()) {
    if (g.appid == appid) return g.name;
  }
  return std::to_string(appid);
}

int cmd_lock() {
  auto run = SteamLibrary::running();
  if (run.empty()) {
    std::cout << "intet spil kører (ikke låst)\n";
    return 0;
  }
  for (const auto& r : run) {
    std::cout << "låst  " << r.appid << "  " << name_for_appid(r.appid) << "  pid "
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
      std::cout << "intet spil kører\n";
      return 0;
    }
    appid = run.front().appid;
    name = name_for_appid(appid);
  }
  std::cout << "stopper " << name << "  " << appid << "\n";
  int n = SteamLibrary::stop(appid);
  if (n == 0 && SteamLibrary::running().empty()) {
    std::cout << "intet spil kører\n";
    return 0;
  }
  auto left = SteamLibrary::running();
  if (!left.empty()) {
    std::cerr << "nfc: spillet kører stadig\n";
    return 1;
  }
  std::cout << "stoppet\n";
  return 0;
}

int cmd_games() {
  auto lib = SteamLibrary::scan();
  if (lib.games().empty()) {
    std::cout << "ingen Steam-spil fundet\n";
    return 0;
  }
  for (const auto& g : lib.games()) {
    std::cout << g.appid << "  " << g.kind << "  " << g.name << "\n";
  }
  std::cout << lib.games().size() << " spil\n";
  return 0;
}

int cmd_list() {
  auto store = TagStore::load(TagStore::default_path());
  if (store.all().empty()) {
    std::cout << "ingen tags i " << store.path().string() << "\n";
    return 0;
  }
  for (const auto& t : store.all()) {
    std::cout << t.uid << "  " << t.kind << "  " << t.appid << "  " << t.name << "\n";
  }
  return 0;
}

int cmd_read() {
  auto store = TagStore::load(TagStore::default_path());
  auto reader = Acr122::open();
  auto uid = wait_for_tag(reader);
  print_tag(Acr122::uid_hex(uid), store);
  reader.set_led(Acr122::Led::Green);
  return 0;
}

int cmd_add(const std::string& query) {
  SteamGame game;
  int rc = resolve_game(query, game);
  if (rc != 0) return rc;
  std::cout << "spil " << game.name << "  " << game.appid << "  " << game.kind << "\n";
  auto store = TagStore::load(TagStore::default_path());
  auto reader = Acr122::open();
  auto uid = wait_for_tag(reader);
  const std::string hex = Acr122::uid_hex(uid);
  store.upsert(Tag{hex, game.name, game.appid, game.kind});
  reader.set_led(Acr122::Led::Green);
  std::cout << "gemt " << game.name << "  " << hex << "\n";
  std::cout << "fil  " << store.path().string() << "\n";
  return 0;
}

int cmd_remove(const std::string& key) {
  auto store = TagStore::load(TagStore::default_path());
  std::string target = key;
  if (target.empty()) {
    auto reader = Acr122::open();
    auto uid = wait_for_tag(reader);
    target = Acr122::uid_hex(uid);
    auto known = store.find_uid(target);
    if (!store.remove(target)) {
      reader.set_led(Acr122::Led::Red);
      std::cout << "uid  " << target << "\n";
      std::cout << "ikke gemt\n";
      return 1;
    }
    reader.blink(Acr122::Led::Red, Acr122::Led::Green, 150ms, 80ms, 2, true);
    std::cout << "fjernet " << (known ? known->name : target) << "  " << target << "\n";
    return 0;
  }
  auto known = store.find(target);
  if (!store.remove(target)) {
    std::cerr << "nfc: ikke fundet: " << target << "\n";
    return 1;
  }
  std::cout << "fjernet " << (known ? known->name : target);
  if (known) std::cout << "  " << known->uid;
  std::cout << "\n";
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
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

    auto reader = Acr122::open();
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
    return 0;
  } catch (const Acr122Error& e) {
    std::cerr << "nfc: " << e.what() << "\n";
    return 1;
  } catch (const std::exception& e) {
    std::cerr << "nfc: " << e.what() << "\n";
    return 1;
  }
}
