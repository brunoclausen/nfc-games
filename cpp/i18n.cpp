#include "i18n.hpp"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {

struct Msg {
  const char* key;
  const char* da;
  const char* en;
};

constexpr Msg kMsg[] = {
    {"help_text",
     "nfc — ACR122U NFC-tags og Steam-spil\n"
     "\n"
     "Brug:\n"
     "  nfc --help\n"
     "  nfc -h\n"
     "  nfc <kommando> [argumenter]\n"
     "\n"
     "Tags og spil:\n"
     "  nfc games                   auto-scan Steam-spil og shortcuts\n"
     "  nfc watch                   klar=grøn; tag på=gul+beep+start; tag af=stop+grøn; stop=rød\n"
     "  nfc start <spil|appid>      start via Steam (spil + emu-genveje)\n"
     "  nfc start                   læs tag og start bundet spil\n"
     "  nfc stop [spil|appid]       stop kørende spil\n"
     "  nfc lock                    vis hvilket spil der er låst/kører\n"
     "  nfc add <spil|appid>        find spil, læs tag, bind dem\n"
     "  nfc read                    læs tag (UID + bundet spil)\n"
     "  nfc list                    vis gemte tags (tags.conf)\n"
     "  nfc remove <spil|uid>       fjern gemt tag\n"
     "  nfc remove                  læs tag og fjern det hvis det er gemt\n"
     "\n"
     "Sprog:\n"
     "  nfc sprog                   vis sprog og hvilket der er aktivt\n"
     "  nfc sprog <kode>            skift sprog (da, en) og gem det\n"
     "  nfc lang                    samme som nfc sprog\n"
     "\n"
     "Læser:\n"
     "  nfc farver                  test rød/grøn/gul LED + beep\n"
     "  nfc led green|red|yellow|off\n"
     "  nfc beep [ms]               bip (standard 200)\n"
     "  nfc firmware                vis ACR122U-firmware\n"
     "  nfc udev                    vis udev-regel til ACR122U\n"
     "  nfc udev install            installér udev-regel (pkexec)\n"
     "\n"
     "Hjælp:\n"
     "  nfc --help, nfc -h          denne tekst\n"
     "  nfc help                    vis HELP.md på aktivt sprog\n"
     "\n"
     "Filer:\n"
     "  tags.conf                   gemte tags i den mappe, du kører fra\n"
     "  HELP.md                     ~/nfc-games/HELP.md\n"
     "  ~/.config/nfc-games/nfc.conf  valgt sprog\n"
     "\n"
     "Eksempel:\n"
     "  nfc games\n"
     "  nfc add \"BloodRayne 2\"\n"
     "  nfc watch\n"
     "  nfc start \"BloodRayne 2\"\n"
     "  nfc lock\n"
     "  nfc stop\n"
     "  nfc add 3640596865\n"
     "  nfc read\n"
     "  nfc sprog\n"
     "  nfc sprog en\n",
     "nfc — ACR122U NFC tags and Steam games\n"
     "\n"
     "Usage:\n"
     "  nfc --help\n"
     "  nfc -h\n"
     "  nfc <command> [arguments]\n"
     "\n"
     "Tags and games:\n"
     "  nfc games                   auto-scan Steam games and shortcuts\n"
     "  nfc watch                   ready=green; tag on=yellow+beep+start; tag off=stop+green; stop=red\n"
     "  nfc start <game|appid>      start via Steam (games + emu shortcuts)\n"
     "  nfc start                   read tag and start bound game\n"
     "  nfc stop [game|appid]       stop running game\n"
     "  nfc lock                    show which game is locked/running\n"
     "  nfc add <game|appid>        find game, read tag, bind them\n"
     "  nfc read                    read tag (UID + bound game)\n"
     "  nfc list                    show saved tags (tags.conf)\n"
     "  nfc remove <game|uid>       remove saved tag\n"
     "  nfc remove                  read tag and remove it if saved\n"
     "\n"
     "Language:\n"
     "  nfc lang                    list languages and the active one\n"
     "  nfc lang <code>             switch language (da, en) and save it\n"
     "  nfc sprog                   same as nfc lang\n"
     "\n"
     "Reader:\n"
     "  nfc farver                  test red/green/yellow LED + beep\n"
     "  nfc led green|red|yellow|off\n"
     "  nfc beep [ms]               beep (default 200)\n"
     "  nfc firmware                show ACR122U firmware\n"
     "  nfc udev                    show udev rule for ACR122U\n"
     "  nfc udev install            install udev rule (pkexec)\n"
     "\n"
     "Help:\n"
     "  nfc --help, nfc -h          this text\n"
     "  nfc help                    show HELP.md in the active language\n"
     "\n"
     "Files:\n"
     "  tags.conf                   saved tags in the directory you run from\n"
     "  HELP.md                     ~/nfc-games/HELP.md\n"
     "  ~/.config/nfc-games/nfc.conf  selected language\n"
     "\n"
     "Example:\n"
     "  nfc games\n"
     "  nfc add \"BloodRayne 2\"\n"
     "  nfc watch\n"
     "  nfc start \"BloodRayne 2\"\n"
     "  nfc lock\n"
     "  nfc stop\n"
     "  nfc add 3640596865\n"
     "  nfc read\n"
     "  nfc lang\n"
     "  nfc lang en\n"},
    {"help_missing", "help-fil: HELP.md (ikke fundet)", "help file: HELP.md (not found)"},
    {"unknown_led", "ukendt LED: ", "unknown LED: "},
    {"led_red", "rød", "red"},
    {"led_green", "grøn", "green"},
    {"led_yellow", "gul (rød+grøn)", "yellow (red+green)"},
    {"led_not_listening", "LED rød (lytter ikke)", "LED red (not listening)"},
    {"game", "spil", "game"},
    {"game_unknown", "spil (ukendt)", "game (unknown)"},
    {"wait_tag", "læg et tag på læseren...", "place a tag on the reader..."},
    {"no_match", "nfc: intet Steam-spil matcher ", "nfc: no Steam game matches "},
    {"run_games", "kør: nfc games", "run: nfc games"},
    {"multi_match", "nfc: flere spil matcher, brug appid:",
     "nfc: several games match, use appid:"},
    {"watch_not_listening", "watch  lytter ikke (rød)", "watch  not listening (red)"},
    {"watch_ready", "watch  klar (grøn)", "watch  ready (green)"},
    {"watch_listening_again", "watch  lytter igen", "watch  listening again"},
    {"watch_switch_stop", "watch  skifter tag, stopper ", "watch  switching tag, stopping "},
    {"watch_unknown_tag", "watch  ukendt tag ", "watch  unknown tag "},
    {"watch_tag_on", "watch  tag på  ", "watch  tag on  "},
    {"watch_start_steam", "watch  starter via Steam  ", "watch  starting via Steam  "},
    {"watch_tag_off", "watch  tag af", "watch  tag off"},
    {"watch_stopping", "watch  stopper ", "watch  stopping "},
    {"watch_stopped", "watch  stoppet (rød, lytter ikke)", "watch  stopped (red, not listening)"},
    {"tag_unbound", "nfc: tagget er ikke bundet til et spil",
     "nfc: tag is not bound to a game"},
    {"already_running", "kører allerede ", "already running "},
    {"locked", "nfc: spil er låst (appid ", "nfc: game is locked (appid "},
    {"locked_suffix", " kører). nfc stop først", " is running). nfc stop first"},
    {"start_steam", "starter via Steam  ", "starting via Steam  "},
    {"nothing_running_unlocked", "intet spil kører (ikke låst)", "no game running (unlocked)"},
    {"locked_line", "låst  ", "locked  "},
    {"nothing_running", "intet spil kører", "no game running"},
    {"stopping", "stopper ", "stopping "},
    {"still_running", "nfc: spillet kører stadig", "nfc: game is still running"},
    {"stopped", "stoppet", "stopped"},
    {"no_steam_games", "ingen Steam-spil fundet", "no Steam games found"},
    {"games_count_suffix", " spil", " games"},
    {"no_tags", "ingen tags i ", "no tags in "},
    {"saved", "gemt ", "saved "},
    {"file", "fil  ", "file  "},
    {"not_saved", "ikke gemt", "not saved"},
    {"removed", "fjernet ", "removed "},
    {"not_found", "nfc: ikke fundet: ", "nfc: not found: "},
    {"lang_active", "aktiv", "active"},
    {"lang_set", "sprog sat til ", "language set to "},
    {"lang_unknown", "nfc: ukendt sprog. Vælg et af:",
     "nfc: unknown language. Choose one of:"},
    {"lang_saved", "gemt i ", "saved in "},
    {"no_reader", "Ingen ACR122U fundet (USB 072f:2200). Er læseren sat i?",
     "No ACR122U found (USB 072f:2200). Is the reader plugged in?"},
    {"claim_fail", "Kunne ikke claim USB-interface: ",
     "Could not claim USB interface: "},
    {"claim_hint", " (kør med sudo eller installer udev-reglen)",
     " (run with sudo or install the udev rule)"},
    {"timeout_tag", "timeout: intet tag", "timeout: no tag"},
    {"ccid_short", "CCID-svar for kort", "CCID reply too short"},
    {"ccid_unexpected", "Uventet CCID-svar", "Unexpected CCID reply"},
    {"ccid_trunc", "CCID payload afkortet", "CCID payload truncated"},
    {"apdu_fail", "APDU fejlede SW=", "APDU failed SW="},
    {"empty_name", "tomt navn", "empty name"},
    {"uid_short", "UID for kort", "UID too short"},
    {"cannot_write", "kan ikke skrive ", "cannot write "},
    {"missing_appid", "spil mangler appid", "game is missing appid"},
    {"steam_fork", "kunne ikke starte Steam", "could not start Steam"},
    {"udev_missing", "udev-regel ikke fundet", "udev rule not found"},
    {"udev_install", "installér (én gang):", "install (once):"},
    {"udev_replug", "træk USB ud og sæt i igen", "unplug USB and plug it back in"},
    {"udev_ok", "udev-regel installeret", "udev rule installed"},
    {"udev_fail", "nfc: kunne ikke installere udev-regel", "nfc: could not install udev rule"},
    {"udev_helper_missing", "nfc: udev-installer mangler i AppImage",
     "nfc: udev installer missing from AppImage"},
};

std::string g_code = "da";

std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

std::string trim(std::string s) {
  auto is_sp = [](unsigned char c) { return std::isspace(c) != 0; };
  while (!s.empty() && is_sp(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
  while (!s.empty() && is_sp(static_cast<unsigned char>(s.back()))) s.pop_back();
  return s;
}

std::string normalize_code(std::string_view raw) {
  std::string s = lower(trim(std::string{raw}));
  if (s == "dk" || s == "dansk" || s == "danish") return "da";
  if (s == "us" || s == "gb" || s == "engelsk" || s == "english") return "en";
  return s;
}

bool known_code(const std::string& code) {
  for (const auto& lang : languages()) {
    if (code == lang.code) return true;
  }
  return false;
}

std::filesystem::path config_dir() {
  if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) {
    return std::filesystem::path(xdg) / "nfc-games";
  }
  const char* home = std::getenv("HOME");
  return std::filesystem::path(home ? home : "/var/home/bruno") / ".config" / "nfc-games";
}

std::string read_config_lang() {
  std::ifstream in(lang_config_path());
  if (!in) return {};
  std::string line;
  while (std::getline(in, line)) {
    auto eq = line.find('=');
    if (eq == std::string::npos) continue;
    auto key = trim(line.substr(0, eq));
    auto val = trim(line.substr(eq + 1));
    if (key == "lang" || key == "sprog") return normalize_code(val);
  }
  return {};
}

}  // namespace

std::vector<Language> languages() {
  return {
      {"da", "Dansk"},
      {"en", "English"},
  };
}

std::filesystem::path lang_config_path() { return config_dir() / "nfc.conf"; }

std::string current_lang_code() { return g_code; }

const char* current_lang_name() {
  for (const auto& lang : languages()) {
    if (g_code == lang.code) return lang.name;
  }
  return g_code.c_str();
}

void i18n_init() {
  g_code = "da";
  if (auto from_file = read_config_lang(); !from_file.empty() && known_code(from_file)) {
    g_code = from_file;
  }
  if (const char* env = std::getenv("NFC_LANG"); env && *env) {
    auto code = normalize_code(env);
    if (known_code(code)) g_code = code;
  }
}

bool set_lang(std::string_view code) {
  auto norm = normalize_code(code);
  if (!known_code(norm)) return false;
  g_code = norm;
  auto dir = config_dir();
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  auto path = lang_config_path();
  auto tmp = path;
  tmp += ".tmp";
  std::ofstream out(tmp, std::ios::trunc);
  if (!out) return false;
  out << "lang=" << g_code << "\n";
  out.close();
  if (!out) return false;
  std::filesystem::rename(tmp, path, ec);
  return !ec;
}

const char* t(const char* key) {
  const bool en = g_code == "en";
  for (const auto& m : kMsg) {
    if (std::strcmp(m.key, key) == 0) return en ? m.en : m.da;
  }
  return key;
}

std::string t_join(const char* key, const std::string& extra) { return std::string(t(key)) + extra; }
