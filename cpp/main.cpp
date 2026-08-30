#include "acr122.hpp"

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
      << "  nfc              demo: grøn, beep, rød, beep, grøn\n"
      << "  nfc firmware     vis firmware-streng\n"
      << "  nfc led green    tænd grøn LED\n"
      << "  nfc led red      tænd rød LED\n"
      << "  nfc led yellow   tænd begge (gul)\n"
      << "  nfc led off      sluk LED\n"
      << "  nfc beep [ms]    bip (standard 200 ms)\n";
}

Acr122::Led parse_led(const std::string& name) {
  if (name == "green" || name == "gron" || name == "grøn") return Acr122::Led::Green;
  if (name == "red" || name == "rod" || name == "rød") return Acr122::Led::Red;
  if (name == "yellow" || name == "gul") return Acr122::Led::Yellow;
  if (name == "off" || name == "sluk") return Acr122::Led::Off;
  throw Acr122Error("ukendt LED: " + name);
}

void demo(Acr122& r) {
  std::cout << "firmware: " << r.firmware() << "\n";
  std::cout << "LED grøn\n";
  r.set_led(Acr122::Led::Green);
  std::this_thread::sleep_for(400ms);

  std::cout << "beep\n";
  r.beep(200ms);
  std::this_thread::sleep_for(200ms);

  std::cout << "LED rød + beep\n";
  r.blink(Acr122::Led::Red, Acr122::Led::Red, 300ms, 100ms, 1, true);
  std::this_thread::sleep_for(200ms);

  std::cout << "blink gul + beep\n";
  r.blink(Acr122::Led::Yellow, Acr122::Led::Green, 150ms, 150ms, 3, true);

  std::cout << "LED grøn (klar)\n";
  r.set_led(Acr122::Led::Green);
}

}  // namespace

int main(int argc, char** argv) {
  try {
    auto reader = Acr122::open();
    if (argc < 2) {
      demo(reader);
      return 0;
    }
    const std::string cmd = argv[1];
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
    } else if (cmd == "demo") {
      demo(reader);
    } else if (cmd == "-h" || cmd == "--help" || cmd == "help") {
      usage();
      return 0;
    } else {
      usage();
      return 2;
    }
    return 0;
  } catch (const Acr122Error& e) {
    std::cerr << "nfc: " << e.what() << "\n";
    return 1;
  }
}
