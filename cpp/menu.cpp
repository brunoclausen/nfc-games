#include "menu.hpp"
#include "acr122.hpp"
#include "cmds.hpp"
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

void print_block(const char* key) {
  const std::string s = t(key);
  std::cout << "\n" << s;
  if (s.empty() || s.back() != '\n') std::cout << '\n';
}

std::string ask_line(const char* key) {
  std::cout << t(key) << std::flush;
  std::string line;
  if (!std::getline(std::cin, line)) return {};
  return trim_line(line);
}

int run_cmd(const std::string& cmd, const std::string& arg, bool typed) {
  try {
    const int rc = nfc_run(cmd, arg);
    if (rc != 0 && !typed) std::cerr << t("menu_fail") << "\n";
    return rc;
  } catch (const std::exception& e) {
    // Acr122Error derives from this. Anything narrower would let a plain
    // runtime_error escape to main() and close the whole menu.
    std::cerr << "nfc: " << e.what() << "\n";
    return 1;
  }
}

void pause_menu() {
  std::cout << t("menu_enter") << std::flush;
  std::string dummy;
  std::getline(std::cin, dummy);
}

bool more_menu() {
  while (true) {
    print_block("menu_more_text");
    std::cout << t("menu_prompt") << std::flush;
    std::string line;
    if (!std::getline(std::cin, line)) return false;
    line = trim_line(line);
    if (line.empty()) continue;
    if (line == "0" || line == "q" || line == "tilbage" || line == "back") return true;
    std::string cmd;
    std::string arg;
    bool block = false;
    if (line == "1") {
      cmd = "games";
    } else if (line == "2") {
      cmd = "start";
      arg = ask_line("menu_ask_game");
    } else if (line == "3") {
      cmd = "stop";
      arg = ask_line("menu_ask_game");
    } else if (line == "4") {
      cmd = "lock";
    } else if (line == "5") {
      cmd = "farver";
    } else if (line == "6") {
      cmd = "firmware";
    } else if (line == "7") {
      cmd = "udev";
      arg = "install";
    } else if (line == "8") {
      cmd = "tag";
      arg = "backup";
    } else if (line == "9") {
      cmd = "tag";
      arg = "restore " + ask_line("menu_ask_restore");
    } else if (line == "10") {
      cmd = "help";
    } else if (line == "11") {
      cmd = "version";
    } else if (line == "12") {
      cmd = "restart";
    } else if (line == "13") {
      if (const int pid = live_watch_pid()) {
        std::cout << t("watch_already") << pid << ")\n";
        pause_menu();
      } else {
        cmd = "watch";
        block = true;
      }
    } else {
      cmd = line;
    }
    run_cmd(cmd, arg, cmd == line);
    if (block) return true;
    pause_menu();
  }
}

}  // namespace

int cmd_menu() {
  while (true) {
    print_block("menu_text");
    std::cout << t("menu_prompt") << std::flush;
    std::string line;
    if (!std::getline(std::cin, line)) break;
    line = trim_line(line);
    if (line.empty()) continue;
    if (line == "0" || line == "q" || line == "quit" || line == "exit" || line == "afslut") {
      return 0;
    }
    std::string cmd;
    std::string arg;
    bool block = false;
    if (line == "1") {
      cmd = "lyt";
    } else if (line == "2") {
      cmd = "add";
    } else if (line == "3") {
      cmd = "add";
      arg = "action " + ask_line("menu_ask_action");
    } else if (line == "4") {
      const std::string picked = ask_line("menu_ask_emu");
      cmd = "add";
      arg = picked.empty() ? "emu" : ("emu " + picked);
    } else if (line == "5") {
      const std::string picked = ask_line("menu_ask_lutris");
      cmd = "add";
      arg = picked.empty() ? "lutris" : ("lutris " + picked);
    } else if (line == "6") {
      cmd = "list";
    } else if (line == "7") {
      cmd = "read";
    } else if (line == "8") {
      cmd = "remove";
      arg = ask_line("menu_ask_remove");
    } else if (line == "9") {
      cmd = "write";
    } else if (line == "10") {
      cmd = "sprog";
      arg = ask_line("menu_ask_lang");
    } else if (line == "11" || line == "m" || line == "mere" || line == "more") {
      if (!more_menu()) return 0;
      continue;
    } else {
      cmd = line;
    }
    const bool typed = (cmd == line);
    run_cmd(cmd, arg, typed);
    if (block) continue;
    if (cmd != "watch") pause_menu();
  }
  return 0;
}
