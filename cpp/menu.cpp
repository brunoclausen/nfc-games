#include "menu.hpp"
#include "acr122.hpp"
#include "commands.hpp"
#include "i18n.hpp"

#include <cctype>
#include <iostream>
#include <string>

namespace {

std::string trim_line(std::string s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
  return s;
}

std::string ask_line(const char* key) {
  std::cout << t(key) << std::flush;
  std::string line;
  if (!std::getline(std::cin, line)) return {};
  return trim_line(line);
}

}  // namespace

int cmd_menu() {
  while (true) {
    std::cout << "\n" << t("menu_text") << t("menu_prompt") << std::flush;
    std::string line;
    if (!std::getline(std::cin, line)) break;
    line = trim_line(line);
    if (line.empty()) continue;
    if (line == "0" || line == "q" || line == "quit" || line == "exit" || line == "afslut") {
      return 0;
    }
    std::string cmd;
    std::string arg;
    if (line == "1") {
      cmd = "watch";
    } else if (line == "2") {
      cmd = "games";
    } else if (line == "3") {
      cmd = "start";
      arg = ask_line("menu_ask_game");
    } else if (line == "4") {
      cmd = "stop";
      arg = ask_line("menu_ask_game");
    } else if (line == "5") {
      cmd = "lock";
    } else if (line == "6") {
      cmd = "add";
      arg = ask_line("menu_ask_game");
      if (arg.empty()) {
        std::cerr << t("menu_need_game") << "\n";
        continue;
      }
    } else if (line == "7") {
      cmd = "read";
    } else if (line == "8") {
      cmd = "list";
    } else if (line == "9") {
      cmd = "remove";
      arg = ask_line("menu_ask_remove");
    } else if (line == "10") {
      cmd = "farver";
    } else if (line == "11") {
      cmd = "led";
      arg = ask_line("menu_ask_led");
    } else if (line == "12") {
      cmd = "beep";
    } else if (line == "13") {
      cmd = "firmware";
    } else if (line == "14") {
      cmd = "udev";
      arg = "install";
    } else if (line == "15") {
      cmd = "sprog";
      arg = ask_line("menu_ask_lang");
    } else if (line == "16") {
      cmd = "help";
    } else if (line == "17") {
      cmd = "version";
    } else if (line == "18") {
      cmd = "restart";
    } else {
      cmd = line;
    }
    try {
      const int rc = nfc_run(cmd, arg);
      if (rc != 0 && cmd != line) std::cerr << t("menu_fail") << "\n";
    } catch (const Acr122Error& e) {
      std::cerr << "nfc: " << e.what() << "\n";
    }
    if (cmd != "watch") {
      std::cout << t("menu_enter") << std::flush;
      std::string dummy;
      std::getline(std::cin, dummy);
    }
  }
  return 0;
}
