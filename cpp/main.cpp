#include "acr122.hpp"
#include "steam.hpp"
#include "tags.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

using namespace std::chrono_literals;

namespace {

void usage() {
  std::cerr
      << "brug: nfc [kommando]\n"
      << "  nfc read                 læs tag (UID)\n"
      << "  nfc add <navn>           læs tag og gem det\n"
      << "  nfc remove <navn|uid>    fjern gemt tag\n"
      << "  nfc remove               læs tag og fjern det hvis det er gemt\n"
      << "  nfc list                 vis gemte tags\n"
      << "  nfc games                auto-scan Steam-spil og shortcuts\n"
      << "  nfc farver               rød, grøn, gul + beep\n"
      << "  nfc led green|red|yellow|off\n"
      << "  nfc beep [ms]\n"
      << "  nfc firmware\n";
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
    std::cout << "navn " << known->name << "\n";
  } else {
    std::cout << "navn (ukendt)\n";
  }
}

std::vector<uint8_t> wait_for_tag(Acr122& reader) {
  std::cout << "læg et tag på læseren...\n" << std::flush;
  return reader.wait_uid(wait_timeout());
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
    std::cout << t.name << "  " << t.uid << "\n";
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

int cmd_add(const std::string& name) {
  auto store = TagStore::load(TagStore::default_path());
  auto reader = Acr122::open();
  auto uid = wait_for_tag(reader);
  const std::string hex = Acr122::uid_hex(uid);
  store.upsert(hex, name);
  reader.set_led(Acr122::Led::Green);
  std::cout << "gemt " << name << "  " << hex << "\n";
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
    if (argc < 2) {
      usage();
      return 2;
    }
    const std::string cmd = argv[1];
    if (cmd == "-h" || cmd == "--help" || cmd == "help") {
      usage();
      return 0;
    }
    if (cmd == "list" || cmd == "ls") return cmd_list();
    if (cmd == "games" || cmd == "spil" || cmd == "steam") return cmd_games();
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
