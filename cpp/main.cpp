#include "acr122.hpp"
#include "app.hpp"
#include "commands.hpp"
#include "i18n.hpp"
#include "menu.hpp"

#include <iostream>
#include <string>
#include <unistd.h>

#ifndef NFC_VERSION
#define NFC_VERSION "dev"
#endif

int main(int argc, char** argv) {
  i18n_init();
  try {
    for (int i = 1; i < argc; ++i) {
      const std::string a = argv[i];
      if (a == "--help" || a == "-h") {
        print_help(std::cout);
        return 0;
      }
      if (a == "--version") {
        std::cout << "nfc-games " << NFC_VERSION << "\n";
        return 0;
      }
    }
    if (argc < 2) {
      if (::isatty(STDIN_FILENO)) return cmd_menu();
      usage();
      return 2;
    }
    const std::string cmd = argv[1];
    if (cmd == "menu" || cmd == ".h" || cmd == "h") return cmd_menu();
    return nfc_run(cmd, join_args(argc, argv, 2));
  } catch (const Acr122Error& e) {
    std::cerr << "nfc: " << e.what() << "\n";
    return 1;
  } catch (const std::exception& e) {
    std::cerr << "nfc: " << e.what() << "\n";
    return 1;
  }
}
