#include "cmds.hpp"
#include "app.hpp"
#include "commands.hpp"
#include "i18n.hpp"
#include "watch.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>

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

int nfc_run(const std::string& cmd, const std::string& arg) {
  if (cmd == "help" || cmd == "hjælp") return cmd_help();
  if (cmd == "version") {
    std::cout << "nfc-games " << NFC_VERSION << "\n";
    return 0;
  }
  if (cmd == "list" || cmd == "ls") return cmd_list();
  if (cmd == "games" || cmd == "spil" || cmd == "steam") return cmd_games();
  if (cmd == "watch" || cmd == "run") {
    nfc_watch_arm();
    return cmd_watch();
  }
  if (cmd == "start" || cmd == "play" || cmd == "launch") return cmd_start(arg);
  if (cmd == "stop" || cmd == "luk") return cmd_stop(arg);
  if (cmd == "lock" || cmd == "laas" || cmd == "lås") return cmd_lock();
  if (cmd == "read" || cmd == "læs" || cmd == "laes") return cmd_read();
  if (cmd == "add" || cmd == "tilfoj" || cmd == "tilføj") {
    if (arg.empty()) {
      usage();
      return 2;
    }
    return cmd_add(arg);
  }
  if (cmd == "remove" || cmd == "rm" || cmd == "slet") return cmd_remove(arg);
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
