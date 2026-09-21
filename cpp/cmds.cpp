#include "cmds.hpp"
#include "app.hpp"
#include "commands.hpp"
#include "i18n.hpp"
#include "launch.hpp"
#include "lutris.hpp"
#include "plat.hpp"
#include "roms.hpp"
#include "watch.hpp"

#include <cctype>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

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

std::string trim_arg(std::string s) {
  auto is_sp = [](unsigned char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
  while (!s.empty() && is_sp(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
  while (!s.empty() && is_sp(static_cast<unsigned char>(s.back()))) s.pop_back();
  return s;
}

std::string lower_ascii(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

std::string shell_single_quote(const std::string& s) {
  std::string out = "'";
  for (char c : s) {
    if (c == '\'') out += "'\\''";
    else out.push_back(c);
  }
  out.push_back('\'');
  return out;
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
  Tag tag;
  if (query.empty()) {
    auto store = TagStore::load(TagStore::default_path());
    UsbPause pause;
    auto reader = open_reader();
    auto uid = wait_for_tag(reader);
    const std::string hex = Acr122::uid_hex(uid);
    auto known = store.find_uid(hex);
    if (!known || (known->appid == 0 && known->target.empty())) {
      reader.set_led(Acr122::Led::Red);
      std::cout << "uid  " << hex << "\n";
      std::cerr << t("tag_unbound") << "\n";
      return 1;
    }
    tag = *known;
    reader.blink(Acr122::Led::Yellow, Acr122::Led::Red, 150ms, 80ms, 1, true);
  } else {
    const auto sp = query.find(' ');
    const std::string first = sp == std::string::npos ? query : query.substr(0, sp);
    const std::string rest0 =
        sp == std::string::npos ? std::string{} : trim_arg(query.substr(sp + 1));
    if (first == "lutris" || first == "heroic") {
      if (rest0.empty()) {
        std::cerr << t("add_kind_usage") << "\n";
        return 2;
      }
      tag.kind = first;
      const auto sp2 = rest0.find(' ');
      if (sp2 == std::string::npos) {
        tag.target = rest0;
        tag.name = rest0;
      } else {
        tag.target = rest0.substr(0, sp2);
        tag.name = trim_arg(rest0.substr(sp2 + 1));
      }
    } else if (first == "action") {
      if (rest0.empty()) {
        std::cerr << t("add_action_usage") << "\n";
        return 2;
      }
      const auto parts = split_action(rest0);
      tag.kind = first;
      tag.target = parts.first;
      tag.target_off = parts.second;
      tag.name = parts.first;
    } else if (first == "emu") {
      if (rest0.empty()) {
        std::cerr << t("add_emu_usage") << "\n";
        return 2;
      }
      tag.kind = first;
      tag.target = rest0;
      tag.name = rest0;
    } else {
      SteamGame game;
      int rc = resolve_game(query, game);
      if (rc != 0) return rc;
      tag.appid = game.appid;
      tag.name = game.name;
      tag.kind = game.kind;
    }
  }

  if (tag.kind == "action") {
    std::cout << t("start_action") << tag.target << "\n";
    launcher::run_detached(tag.target);
    return 0;
  }

  if (launcher::is_emu_kind(tag.kind)) {
    std::cout << t("start_emu") << tag.target << "\n";
    launcher::run_detached(tag.target);
    return 0;
  }

  if (launcher::is_steam_kind(tag.kind)) {
    SteamGame game;
    game.appid = tag.appid;
    game.name = tag.name;
    game.kind = tag.kind;
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

  std::cout << t("start_other") << tag.name << "  " << launcher::uri(tag) << "\n";
  launcher::launch(tag);
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
    {
      // Lutris/Heroic targets have no automatic stop; say so before the
      // Steam-only resolver reports "no match".
      auto store = TagStore::load(TagStore::default_path());
      const std::string uid = normalize_uid(query);
      for (const auto& tag : store.all()) {
        if (launcher::is_steam_kind(tag.kind)) continue;
        if (tag.uid == uid || tag.target == query || lower_ascii(tag.name) == lower_ascii(query)) {
          std::cerr << t("stop_not_stoppable") << "\n";
          return 2;
        }
      }
    }
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

int cmd_lutris() {
  auto lib = LutrisLibrary::scan();
  if (lib.games().empty()) {
    std::cout << t("no_lutris_games") << "\n";
    return 0;
  }
  for (const auto& g : lib.games()) {
    std::cout << g.slug << "  " << g.runner << "  " << g.name << (g.installed() ? "" : "  [ikke installeret]") << "\n";
  }
  std::cout << lib.games().size() << t("games_count_suffix") << "\n";
  return 0;
}

int cmd_lutris_install(const std::string& arg) {
  if (arg.empty()) {
    std::cerr << t("lutris_install_usage") << "\n";
    return 2;
  }
  auto lib = LutrisLibrary::scan();
  auto it = std::find_if(lib.games().begin(), lib.games().end(),
    [&](const LutrisGame& g) { return g.slug == arg || g.name == arg; });
  LutrisGame found_game;
  bool found = false;
  if (it != lib.games().end()) {
    found_game = *it;
    found = true;
  } else {
    // Also check GOG service database
    auto gog_lib = LutrisLibrary::scan_gog();
    auto gog_it = std::find_if(gog_lib.games().begin(), gog_lib.games().end(),
      [&](const LutrisGame& g) { return g.slug == arg || g.name == arg; });
    if (gog_it != gog_lib.games().end()) {
      found_game = *gog_it;
      found = true;
    }
  }
  if (!found) {
    std::cerr << t("lutris_not_found") << arg << "\n";
    return 1;
  }
  const LutrisGame& g = found_game;
  if (g.installed()) {
    std::cout << t("lutris_already_installed") << g.name << "\n";
    return 0;
  }
  const std::string bin = LutrisLibrary::find_binary();
  if (bin.empty()) {
    std::cerr << t("lutris_not_available") << "\n";
    return 1;
  }
  std::cout << t("lutris_installing") << g.name << " (" << g.slug << ")...\n";
  std::string cmd = "\"" + bin + "\" lutris:install/" + shell_single_quote(g.slug) + " 2>&1";
  FILE* f = ::popen(cmd.c_str(), "r");
  if (!f) {
    std::cerr << t("lutris_install_failed") << "\n";
    return 1;
  }

  const char* spinner = "|/-\\";
  int spin_idx = 0;
  std::string buffer;
  int last_pct = -1;

  auto flush_spinner = [&](int pct) {
    if (pct >= 0) {
      std::cout << "\r  [" << std::setw(3) << pct << "%] " << spinner[spin_idx % 4] << " " << g.name << "    " << std::flush;
    } else {
      std::cout << "\r  " << spinner[spin_idx % 4] << " " << g.name << "    " << std::flush;
    }
    spin_idx++;
  };

  std::array<char, 4096> buf{};
  std::size_t n = 0;
  auto last_update = std::chrono::steady_clock::now();
  auto handle_line = [&](const std::string& line) {
    int pct = -1;
    size_t pct_pos = line.find('%');
    if (pct_pos != std::string::npos) {
      size_t start = pct_pos;
      while (start > 0 && std::isdigit(static_cast<unsigned char>(line[start - 1]))) start--;
      const std::string num_str = line.substr(start, pct_pos - start);
      try { pct = std::stoi(num_str); } catch (...) {}
    }
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_update).count() > 100) {
      flush_spinner(pct);
      if (pct >= 0) last_pct = pct;
      last_update = now;
    }
  };

  while ((n = std::fread(buf.data(), 1, buf.size(), f)) > 0) {
    buffer.append(buf.data(), n);
    // Lutris/wget overwrites the progress line with \r; split on both.
    while (true) {
      const size_t ln = buffer.find('\n');
      const size_t cr = buffer.find('\r');
      if (ln == std::string::npos && cr == std::string::npos) break;
      const size_t sep = (cr != std::string::npos && (ln == std::string::npos || cr < ln)) ? cr : ln;
      handle_line(buffer.substr(0, sep));
      buffer.erase(0, sep + 1);
      if (sep == cr && buffer.size() && buffer[0] == '\n') buffer.erase(0, 1);
    }
  }
  if (!buffer.empty()) handle_line(buffer);
  flush_spinner(last_pct >= 0 ? last_pct : 100);
  std::cout << "\n";

  int rc = ::pclose(f);
  if (rc == 0) {
    std::cout << t("lutris_install_ok") << g.name << "\n";
  } else {
    std::cerr << t("lutris_install_failed") << "\n";
  }
  return rc == 0 ? 0 : 1;
}

int cmd_emu() {
  auto lib = EmuLibrary::scan();
  if (lib.games().empty()) {
    std::cout << t("no_emu_games") << "\n";
    return 0;
  }
  for (const auto& g : lib.games()) {
    std::cout << g.system << "  " << g.name << "  " << g.command << "\n";
  }
  std::cout << lib.games().size() << t("games_count_suffix") << "\n";
  return 0;
}

int cmd_scan(const std::string& arg) {
  std::string what = arg.empty() ? "all" : arg;
  if (what == "steam" || what == "spil" || what == "games") {
    auto lib = SteamLibrary::cached_scan();
    int n = 0;
    for (const auto& g : lib.games()) {
      if (g.appid == 0) continue;
      ++n;
      std::cout << g.appid << "  " << g.name << "\n";
    }
    std::cout << n << t("games_count_suffix") << "\n";
    return 0;
  }
  if (what == "emu" || what == "roms" || what == "emu-spil") {
    return cmd_emu();
  }
  if (what == "lutris" || what == "lutris-spil") {
    auto lib = LutrisLibrary::scan();
    if (lib.games().empty()) {
      std::cout << t("no_lutris_games") << "\n";
      return 0;
    }
    for (const auto& g : lib.games()) {
      std::cout << g.slug << "  " << g.name << "  [" << g.runner << "]\n";
    }
    std::cout << lib.games().size() << t("games_count_suffix") << "\n";
    return 0;
  }
  if (what == "gog") {
    auto lib = LutrisLibrary::scan_gog();
    if (lib.games().empty()) {
      std::cout << t("no_gog_games") << "\n";
      return 0;
    }
    for (const auto& g : lib.games()) {
      std::cout << g.slug << "  " << g.name << "  [" << g.runner << "]" << (g.installed() ? "" : "  [ikke installeret]") << "\n";
    }
    std::cout << lib.games().size() << t("games_count_suffix") << "\n";
    return 0;
  }
  if (what == "all" || what == "alle" || what == "everything") {
    std::cout << "=== Steam ===\n";
    auto slib = SteamLibrary::cached_scan();
    int n = 0;
    for (const auto& g : slib.games()) {
      if (g.appid == 0) continue;
      ++n;
      std::cout << g.appid << "  " << g.name << "\n";
    }
    std::cout << n << t("games_count_suffix") << "\n\n";

    std::cout << "=== Emu ===\n";
    auto elib = EmuLibrary::scan();
    if (elib.games().empty()) {
      std::cout << t("no_emu_games") << "\n";
    } else {
      for (const auto& g : elib.games()) {
        std::cout << g.system << "  " << g.name << "  " << g.command << "\n";
      }
      std::cout << elib.games().size() << t("games_count_suffix") << "\n";
    }
    std::cout << "\n";

    std::cout << "=== Lutris ===\n";
    auto llib = LutrisLibrary::scan();
    if (llib.games().empty()) {
      std::cout << t("no_lutris_games") << "\n";
    } else {
      for (const auto& g : llib.games()) {
        std::cout << g.slug << "  " << g.name << "  [" << g.runner << "]\n";
      }
      std::cout << llib.games().size() << t("games_count_suffix") << "\n";
    }
    std::cout << "\n";

    std::cout << "=== GOG ===\n";
    auto glib = LutrisLibrary::scan_gog();
    if (glib.games().empty()) {
      std::cout << t("no_gog_games") << "\n";
    } else {
      for (const auto& g : glib.games()) {
        std::cout << g.slug << "  " << g.name << "  [" << g.runner << "]" << (g.installed() ? "" : "  [ikke installeret]") << "\n";
      }
      std::cout << glib.games().size() << t("games_count_suffix") << "\n";
    }
    return 0;
  }
  std::cerr << t("scan_usage") << "\n";
  return 2;
}

int cmd_list() {
  auto store = TagStore::load(TagStore::default_path());
  if (store.all().empty()) {
    std::cout << t("no_tags") << store.path().string() << "\n";
    return 0;
  }
  for (const auto& tag : store.all()) {
    if (tag.kind == "action") {
      std::cout << tag.uid << "  action  " << tag.target;
      if (!tag.target_off.empty()) std::cout << " || " << tag.target_off;
      std::cout << "\n";
      continue;
    }
    if (launcher::is_emu_kind(tag.kind)) {
      std::cout << tag.uid << "  emu  " << tag.target << "\n";
      continue;
    }
    const std::string id = launcher::is_steam_kind(tag.kind)
                               ? std::to_string(tag.appid)
                               : tag.target;
    std::cout << tag.uid << "  " << (tag.kind.empty() ? "steam" : tag.kind) << "  " << id
              << "  " << tag.name << "\n";
  }
  return 0;
}

namespace {

void print_ndef(Acr122& reader) {
  try {
    if (auto text = reader.read_ndef_text()) {
      std::cout << t("ndef_text") << *text << "\n";
    } else {
      std::cout << t("ndef_empty") << "\n";
    }
  } catch (const Acr122Error&) {
    std::cout << t("ndef_empty") << "\n";
  }
}

int write_ndef_game(Acr122& reader, const std::string& name) {
  try {
    reader.write_ndef_text(name);
    std::cout << t("ndef_written") << name << "\n";
    std::cout << t("ndef_ok") << "\n";
    return 0;
  } catch (const Acr122Error& e) {
    std::cerr << t("ndef_write_fail") << e.what() << "\n";
    return 1;
  }
}

}  // namespace

namespace {

const char* g_hex = "0123456789ABCDEF";

std::string hex_byte(uint8_t b) {
  std::string s(2, '0');
  s[0] = g_hex[b >> 4];
  s[1] = g_hex[b & 0x0F];
  return s;
}

std::string hex_bytes(const std::vector<uint8_t>& v, size_t max) {
  std::string s;
  const size_t n = std::min(max, v.size());
  for (size_t i = 0; i < n; ++i) {
    s.push_back(g_hex[v[i] >> 4]);
    s.push_back(g_hex[v[i] & 0x0F]);
  }
  return s;
}

std::vector<uint8_t> unhex_bytes(const std::string& hx) {
  std::vector<uint8_t> out;
  auto val = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  for (size_t i = 0; i + 1 < hx.size(); i += 2) {
    const int hi = val(hx[i]);
    const int lo = val(hx[i + 1]);
    if (hi < 0 || lo < 0) break;
    out.push_back(static_cast<uint8_t>((hi << 4) | lo));
  }
  return out;
}

int cmd_tag_backup(const std::string& file_arg) {
  UsbPause pause;
  auto reader = open_reader();
  auto uid = wait_for_tag(reader);
  const std::string uidhex = Acr122::uid_hex(uid);

  std::vector<std::vector<uint8_t>> pages;
  pages.reserve(232);
  for (uint8_t p = 0; p < 232; ++p) {
    try {
      auto chunk = reader.read_binary(p, 4);
      if (chunk.empty()) break;
      pages.push_back(std::move(chunk));
    } catch (const Acr122Error&) {
      break;
    }
  }

  std::filesystem::path path = file_arg;
  if (path.empty()) {
    auto dir = nfc_config_dir() / "tag-backups";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const auto ts = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch())
                        .count();
    path = dir / (uidhex + "-" + std::to_string(ts) + ".hex");
  } else if (!path.parent_path().empty()) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
  }

  std::ofstream out(path);
  if (!out) {
    std::cerr << t("tag_restore_open") << path << "\n";
    return 1;
  }
  out << "UID " << uidhex << "\n";
  if (auto ver = reader.ntag_version(); ver) out << "VERSION " << hex_bytes(*ver, 8) << "\n";
  for (size_t p = 0; p < pages.size(); ++p) {
    out << hex_byte(static_cast<uint8_t>(p)) << " " << hex_bytes(pages[p], 4) << "\n";
  }
  out.close();

  std::cout << t("tag_backup_uid") << uidhex << "\n";
  std::cout << t("tag_backup_count") << pages.size() << t("tag_backup_suffix") << path << "\n";
  led_not_listening(reader);
  return 0;
}

int cmd_tag_restore(const std::string& file_arg) {
  if (file_arg.empty()) {
    std::cerr << t("tag_restore_usage") << "\n";
    return 2;
  }
  std::ifstream in(file_arg);
  if (!in) {
    std::cerr << t("tag_restore_open") << file_arg << "\n";
    return 1;
  }

  std::vector<std::vector<uint8_t>> pages(232);
  std::string want_uid;
  std::string line;
  while (std::getline(in, line)) {
    const auto sp = line.find_first_of(" \t");
    if (sp == std::string::npos) continue;
    const std::string key = line.substr(0, sp);
    const std::string val = trim_arg(line.substr(sp));
    if (key == "UID") { want_uid = val; continue; }
    if (key == "VERSION") continue;
    long p = strtol(key.c_str(), nullptr, 16);
    if (p < 0 || p > 231) continue;
    auto bytes = unhex_bytes(val);
    if (bytes.size() != 4) continue;
    pages[static_cast<size_t>(p)] = std::move(bytes);
  }

  UsbPause pause;
  auto reader = open_reader();
  auto uid = wait_for_tag(reader);
  const std::string uidhex = Acr122::uid_hex(uid);
  std::cout << t("tag_restore_uid") << uidhex << "\n";
  if (!want_uid.empty() && want_uid != uidhex) {
    std::cerr << t("tag_restore_mismatch") << want_uid << t("tag_restore_mismatch_suffix") << "\n";
    return 2;
  }

  int ok = 0, fail = 0;
  for (size_t p = 0; p < pages.size(); ++p) {
    if (p <= 2) continue;  // UID pages: readonly
    if (pages[p].size() != 4) continue;
    bool zero = true;
    for (uint8_t b : pages[p]) {
      if (b != 0) zero = false;
    }
    if (zero) continue;  // never-written pages
    try {
      reader.write_page(static_cast<uint8_t>(p), pages[p].data());
      ++ok;
    } catch (const Acr122Error&) {
      ++fail;
    }
  }
  std::cout << t("tag_restore_written") << ok << t("tag_restore_and") << fail
            << t("tag_restore_fail_suffix") << "\n";
  print_ndef(reader);
  led_not_listening(reader);
  return 0;
}

}  // namespace

int cmd_read() {
  auto store = TagStore::load(TagStore::default_path());
  UsbPause pause;
  auto reader = open_reader();
  auto uid = wait_for_tag(reader);
  print_tag(Acr122::uid_hex(uid), store);
  print_ndef(reader);
  led_not_listening(reader);
  return 0;
}

namespace {

bool bindable_game(const SteamGame& g) {
  if (g.appid == 0) return false;
  std::string n = g.name;
  for (char& c : n) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (n.find("proton") != std::string::npos) return false;
  if (n.find("steam linux runtime") != std::string::npos) return false;
  if (n.find("steamworks") != std::string::npos) return false;
  if (n.find("boot-windows") != std::string::npos) return false;
  if (n.find("compattool") != std::string::npos) return false;
  return true;
}

std::string pick_game_query() {
  auto lib = SteamLibrary::cached_scan();
  std::vector<SteamGame> shown;
  int n = 0;
  for (const auto& g : lib.games()) {
    if (!bindable_game(g)) continue;
    ++n;
    std::cout << "  " << n << "  " << g.name << "\n";
    shown.push_back(g);
  }
  if (shown.empty()) {
    std::cerr << t("no_steam_games") << "\n";
    return {};
  }
  std::cout << t("menu_ask_game_pick") << std::flush;
  std::string line;
  if (!std::getline(std::cin, line)) return {};
  while (!line.empty() && std::isspace(static_cast<unsigned char>(line.front()))) line.erase(line.begin());
  while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) line.pop_back();
  if (line.empty()) return {};
  char* end = nullptr;
  const long v = std::strtol(line.c_str(), &end, 10);
  if (end != line.c_str() && end && *end == '\0' && v >= 1 &&
      v <= static_cast<long>(shown.size())) {
    return std::to_string(shown[static_cast<std::size_t>(v) - 1].appid);
  }
  return line;
}

std::string pick_lutris_query() {
  auto lib = LutrisLibrary::scan();
  const auto& shown = lib.games();
  if (shown.empty()) {
    std::cerr << t("no_lutris_games") << "\n";
    return {};
  }
  int n = 0;
  for (const auto& g : shown) {
    ++n;
    std::cout << "  " << n << "  " << g.name;
    if (!g.runner.empty()) std::cout << "  [" << g.runner << "]";
    std::cout << "\n";
  }
  std::cout << t("menu_ask_game_pick") << std::flush;
  std::string line;
  if (!std::getline(std::cin, line)) return {};
  while (!line.empty() && std::isspace(static_cast<unsigned char>(line.front()))) line.erase(line.begin());
  while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) line.pop_back();
  if (line.empty()) return {};
  char* end = nullptr;
  const long v = std::strtol(line.c_str(), &end, 10);
  if (end != line.c_str() && end && *end == '\0' && v >= 1 &&
      v <= static_cast<long>(shown.size())) {
    const auto& g = shown[static_cast<std::size_t>(v) - 1];
    return g.slug + " " + g.name;
  }
  if (auto g = lib.find(line)) return g->slug + " " + g->name;
  return line;
}

std::string pick_emu_query() {
  auto lib = EmuLibrary::scan();
  const auto& shown = lib.games();
  if (shown.empty()) {
    std::cerr << t("no_emu_games") << "\n";
    return {};
  }

  std::map<std::string, std::vector<const EmuGame*>> by_system;
  for (const auto& g : shown) {
    by_system[g.system].push_back(&g);
  }

  while (true) {
    std::cout << t("emu_pick_system") << "\n";
    int sys_n = 0;
    std::vector<std::string> systems;
    for (const auto& [sys, games] : by_system) {
      ++sys_n;
      systems.push_back(sys);
      std::cout << "  " << sys_n << "  [" << sys << "] " << games.size() << " spil\n";
    }
    std::cout << t("menu_ask_game_pick") << std::flush;
    std::string line;
    if (!std::getline(std::cin, line)) return {};
    while (!line.empty() && std::isspace(static_cast<unsigned char>(line.front()))) line.erase(line.begin());
    while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) line.pop_back();
    if (line.empty()) return {};

    char* end = nullptr;
    const long v = std::strtol(line.c_str(), &end, 10);
    const std::string* picked_system = nullptr;
    if (end != line.c_str() && end && *end == '\0' && v >= 1 &&
        v <= static_cast<long>(systems.size())) {
      picked_system = &systems[static_cast<std::size_t>(v) - 1];
    } else {
      const std::string needle = lower_ascii(line);
      for (const auto& sys : systems) {
        if (lower_ascii(sys) == needle) { picked_system = &sys; break; }
      }
      if (!picked_system) {
        for (const auto& sys : systems) {
          if (lower_ascii(sys).find(needle) != std::string::npos) { picked_system = &sys; break; }
        }
      }
    }
    if (!picked_system) continue;

    const auto& games = by_system[*picked_system];
    while (true) {
      std::cout << "[" << *picked_system << "] " << t("emu_pick_game") << "\n";
      int n = 0;
      for (const auto* g : games) {
        ++n;
        std::cout << "  " << n << "  " << g->name << "\n";
      }
      std::cout << t("menu_ask_game_pick") << std::flush;
      if (!std::getline(std::cin, line)) return {};
      while (!line.empty() && std::isspace(static_cast<unsigned char>(line.front()))) line.erase(line.begin());
      while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) line.pop_back();
      if (line.empty()) break;

      const long v2 = std::strtol(line.c_str(), &end, 10);
      const EmuGame* pick = nullptr;
      if (end != line.c_str() && end && *end == '\0' && v2 >= 1 &&
          v2 <= static_cast<long>(games.size())) {
        pick = games[static_cast<std::size_t>(v2) - 1];
      } else {
        const std::string needle2 = lower_ascii(line);
        for (const auto* g : games) {
          if (lower_ascii(g->name) == needle2) { pick = g; break; }
        }
        if (!pick) {
          for (const auto* g : games) {
            if (lower_ascii(g->name).find(needle2) != std::string::npos) { pick = g; break; }
          }
        }
      }
      if (pick) return pick->name + "\t" + pick->command;
    }
  }
}

}  // namespace

int cmd_add(const std::string& query) {
  std::string q = trim_arg(query);
  std::string kind, target, off, display;
  std::string first = q;
  std::string rest0;
  const auto sp = q.find(' ');
  if (sp != std::string::npos) {
    first = q.substr(0, sp);
    rest0 = trim_arg(q.substr(sp + 1));
  }
  if (first == "lutris" || first == "heroic") {
    std::string rest = rest0;
    if (rest.empty()) {
      if (!plat::stdin_is_tty()) {
        std::cerr << t("add_kind_usage") << "\n";
        return 2;
      }
      rest = pick_lutris_query();
    }
    if (rest.empty()) {
      std::cerr << t("add_kind_usage") << "\n";
      return 2;
    }
    kind = first;
    const auto sp2 = rest.find(' ');
    if (sp2 == std::string::npos) {
      target = rest;
    } else {
      target = rest.substr(0, sp2);
      display = trim_arg(rest.substr(sp2 + 1));
    }
    if (display.empty()) display = target;
    q.clear();
  } else if (first == "action") {
    const std::string rest = rest0;
    if (rest.empty()) {
      std::cerr << t("add_action_usage") << "\n";
      return 2;
    }
    const auto parts = split_action(rest);
    kind = "action";
    target = parts.first;
    off = parts.second;
    display = parts.first;
    q.clear();
  } else if (first == "emu") {
    std::string rest = rest0;
    if (rest.empty()) {
      if (!plat::stdin_is_tty()) {
        std::cerr << t("add_emu_usage") << "\n";
        return 2;
      }
      rest = pick_emu_query();
    }
    if (rest.empty()) {
      std::cerr << t("add_emu_usage") << "\n";
      return 2;
    }
    const auto tab = rest.find('\t');
    if (tab == std::string::npos) {
      target = rest;
      display = rest;
    } else {
      display = rest.substr(0, tab);
      target = rest.substr(tab + 1);
    }
    kind = "emu";
    q.clear();
  }
  if (kind.empty() && q.empty()) {
    if (!plat::stdin_is_tty()) {
      usage();
      return 2;
    }
    q = pick_game_query();
    if (q.empty()) {
      std::cerr << t("menu_need_game") << "\n";
      return 1;
    }
  }
  SteamGame game;
  if (kind.empty()) {
    int rc = resolve_game(q, game);
    if (rc != 0) return rc;
    std::cout << t("game") << " " << game.name << "  " << game.appid << "  " << game.kind
              << "\n";
  } else {
    game.name = display;
    game.kind = kind;
    std::cout << t("game") << " " << game.name << "  " << kind << "  " << target << "\n";
  }
  auto store = TagStore::load(TagStore::default_path());
  UsbPause pause;
  auto reader = open_reader();
  auto uid = wait_for_tag(reader);
  const std::string hex = Acr122::uid_hex(uid);
  Tag bound{hex, game.name, game.appid, game.kind, target, off};
  store.upsert(std::move(bound));
  (void)write_ndef_game(reader, game.name);
  led_not_listening(reader);
  std::cout << t("saved") << game.name << "  " << hex << "\n";
  std::cout << t("file") << store.path().string() << "\n";
  return 0;
}

int cmd_write() {
  auto store = TagStore::load(TagStore::default_path());
  UsbPause pause;
  auto reader = open_reader();
  auto uid = wait_for_tag(reader);
  const std::string hex = Acr122::uid_hex(uid);
  print_tag(hex, store);
  auto known = store.find_uid(hex);
  if (!known || known->name.empty()) {
    reader.set_led(Acr122::Led::Red);
    std::cerr << t("ndef_unbound") << "\n";
    return 1;
  }
  const int rc = write_ndef_game(reader, known->name);
  led_not_listening(reader);
  return rc;
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

std::filesystem::path extract_embedded_zadig() {
#if !defined(_WIN32) || !defined(NFC_EMBED_ZADIG)
  return {};
#else
  const HRSRC res = ::FindResourceW(nullptr, L"ZADIG", MAKEINTRESOURCEW(10));
  if (!res) return {};
  const HGLOBAL glob = ::LoadResource(nullptr, res);
  if (!glob) return {};
  const void* data = ::LockResource(glob);
  const DWORD size = ::SizeofResource(nullptr, res);
  if (!data || size < 64) return {};

  wchar_t temp[MAX_PATH];
  const DWORD n = ::GetTempPathW(MAX_PATH, temp);
  if (n == 0 || n >= MAX_PATH) return {};
  const std::filesystem::path out = std::filesystem::path(temp) / L"nfc-games-zadig.exe";
  std::error_code ec;
  if (std::filesystem::is_regular_file(out, ec) && std::filesystem::file_size(out, ec) == size) {
    return out;
  }

  const HANDLE file = ::CreateFileW(out.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                     FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return {};
  DWORD written = 0;
  const BOOL ok = ::WriteFile(file, data, size, &written, nullptr);
  ::CloseHandle(file);
  if (!ok || written != size) {
    std::filesystem::remove(out, ec);
    return {};
  }
  return out;
#endif
}

std::filesystem::path zadig_exe() {
  if (const auto embedded = extract_embedded_zadig(); !embedded.empty()) return embedded;
  std::error_code ec;
  auto ok = [&](const std::filesystem::path& p) {
    return std::filesystem::is_regular_file(p, ec);
  };
  if (const auto exe = plat::exe_path(); !exe.empty()) {
    const auto beside = exe.parent_path() / "zadig.exe";
    if (ok(beside)) return beside;
  }
  const auto cwd = std::filesystem::current_path() / "zadig.exe";
  if (ok(cwd)) return cwd;
  return {};
}

int cmd_udev(const std::string& arg) {
#ifdef _WIN32
  if (arg == "install" || arg == "installer") {
    const auto zadig = zadig_exe();
    if (zadig.empty()) {
      std::cerr << t("winusb_missing") << "\n";
      const std::string help = t("winusb_text");
      std::cout << help;
      if (help.empty() || help.back() != '\n') std::cout << '\n';
      return 1;
    }
    try {
      plat::open_uri(zadig.string());
    } catch (const std::exception& e) {
      std::cerr << "nfc: " << e.what() << "\n";
      return 1;
    }
    std::cout << t("winusb_started") << zadig.string() << "\n";
    return 0;
  }
  const std::string help = t("winusb_text");
  std::cout << help;
  if (help.empty() || help.back() != '\n') std::cout << '\n';
  return 0;
#else
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
#endif
}

int live_watch_pid() {
  std::ifstream in(watch_pid_path());
  int pid = 0;
  if (in >> pid && pid > 0 && pid != plat::current_pid() && plat::process_alive(pid)) return pid;
  return 0;
}

namespace {

int run_cmd(std::vector<const char*> argv, bool quiet) {
  std::vector<std::string> args;
  args.reserve(argv.size());
  for (const char* s : argv) args.emplace_back(s);
  return plat::run_wait(args, quiet);
}

int systemd_user(const char* action, bool quiet) {
  return run_cmd({"systemctl", "--user", action, "nfc-games.service"}, quiet);
}

bool systemd_unit_present() {
  std::filesystem::path unit;
  if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) {
    unit = std::filesystem::path(xdg) / "systemd" / "user" / "nfc-games.service";
  } else if (const char* home = std::getenv("HOME"); home && *home) {
    unit = std::filesystem::path(home) / ".config" / "systemd" / "user" / "nfc-games.service";
  }
  std::error_code ec;
  return !unit.empty() && std::filesystem::is_regular_file(unit, ec);
}

int wait_pid_gone(int pid, int ms) {
  using namespace std::chrono_literals;
  for (int waited = 0; waited < ms; waited += 100) {
    if (!plat::process_alive(pid)) return 0;
    std::this_thread::sleep_for(100ms);
  }
  return plat::process_alive(pid) ? 1 : 0;
}

int stop_watch_pid(int pid) {
  if (pid <= 0) return 0;
  plat::signal_term(pid);
  if (wait_pid_gone(pid, 4000) == 0) return 0;
  plat::signal_kill(pid);
  return wait_pid_gone(pid, 1000);
}

std::filesystem::path watch_binary() {
  std::error_code ec;
#ifndef _WIN32
  if (const char* app = std::getenv("APPIMAGE"); app && *app) {
    const std::filesystem::path p(app);
    if (std::filesystem::is_regular_file(p, ec) && plat::is_executable(p)) return p;
  }
#endif
  if (const auto exe = plat::exe_path();
      !exe.empty() && std::filesystem::is_regular_file(exe, ec) && plat::is_executable(exe)) {
    return exe;
  }
#ifndef _WIN32
  const auto bundled = plat::home_dir() / "Applications" / "nfc-games-x86_64.AppImage";
  if (std::filesystem::is_regular_file(bundled, ec) && plat::is_executable(bundled)) return bundled;
#endif
  return {};
}

int spawn_watch() { return plat::spawn_watch(watch_binary()); }

int wait_new_watch_pid(int old, int ms) {
  using namespace std::chrono_literals;
  for (int waited = 0; waited < ms; waited += 100) {
    const int pid = live_watch_pid();
    if (pid > 0 && pid != old) return pid;
    std::this_thread::sleep_for(100ms);
  }
  return live_watch_pid();
}

}  // namespace

std::string pick_lutris_install_query() {
  auto lib = LutrisLibrary::scan();
  const auto& shown = lib.games();
  if (shown.empty()) {
    std::cerr << t("no_lutris_games") << "\n";
    return {};
  }
  int n = 0;
  for (const auto& g : shown) {
    ++n;
    std::cout << "  " << n << "  " << g.name;
    if (!g.runner.empty()) std::cout << "  [" << g.runner << "]";
    if (!g.installed()) std::cout << "  [ikke installeret]";
    std::cout << "\n";
  }
  std::cout << t("menu_ask_game_pick") << std::flush;
  std::string line;
  if (!std::getline(std::cin, line)) return {};
  while (!line.empty() && std::isspace(static_cast<unsigned char>(line.front()))) line.erase(line.begin());
  while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) line.pop_back();
  if (line.empty()) return {};
  std::string lower = line;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (lower == "0" || lower == "q" || lower == "tilbage" || lower == "back") return {};
  char* end = nullptr;
  const long v = std::strtol(line.c_str(), &end, 10);
  if (end != line.c_str() && end && *end == '\0' && v >= 1 &&
      v <= static_cast<long>(shown.size())) {
    const auto& g = shown[static_cast<std::size_t>(v) - 1];
    return g.slug;
  }
  if (auto g = lib.find(line)) return g->slug;
  return line;
}

std::string pick_gog_install_query() {
  auto lib = LutrisLibrary::scan_gog();
  const auto& shown = lib.games();
  if (shown.empty()) {
    std::cerr << t("no_gog_games") << "\n";
    return {};
  }
  int n = 0;
  for (const auto& g : shown) {
    ++n;
    std::cout << "  " << n << "  " << g.name;
    if (!g.runner.empty()) std::cout << "  [" << g.runner << "]";
    if (!g.installed()) std::cout << "  [ikke installeret]";
    std::cout << "\n";
  }
  std::cout << t("menu_ask_game_pick") << std::flush;
  std::string line;
  if (!std::getline(std::cin, line)) return {};
  while (!line.empty() && std::isspace(static_cast<unsigned char>(line.front()))) line.erase(line.begin());
  while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) line.pop_back();
  if (line.empty()) return {};
  std::string lower = line;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (lower == "0" || lower == "q" || lower == "tilbage" || lower == "back") return {};
  char* end = nullptr;
  const long v = std::strtol(line.c_str(), &end, 10);
  if (end != line.c_str() && end && *end == '\0' && v >= 1 &&
      v <= static_cast<long>(shown.size())) {
    const auto& g = shown[static_cast<std::size_t>(v) - 1];
    return g.slug;
  }
  if (auto g = lib.find(line)) return g->slug;
  return line;
}

int cmd_start_watch() {
  if (const int pid = live_watch_pid()) {
    std::cout << t("watch_running") << pid << ")\n";
    return 0;
  }
  std::cout << t("watch_starting") << "\n" << std::flush;
  if (systemd_unit_present()) {
    systemd_user("daemon-reload", true);
    if (systemd_user("start", true) == 0) {
      using namespace std::chrono_literals;
      for (int i = 0; i < 40; ++i) {
        if (systemd_user("is-active", true) == 0) break;
        std::this_thread::sleep_for(100ms);
      }
      if (systemd_user("is-active", true) == 0) {
        const int pid = wait_new_watch_pid(0, 4000);
        std::cout << t("watch_started") << (pid > 0 ? pid : 0) << ")\n";
        return 0;
      }
    }
  }
  if (spawn_watch() != 0) {
    std::cerr << t("restart_fail") << "\n";
    return 1;
  }
  const int pid = wait_new_watch_pid(0, 4000);
  if (pid <= 0) {
    std::cerr << t("restart_fail") << "\n";
    return 1;
  }
  std::cout << t("watch_started") << pid << ")\n";
  return 0;
}

int cmd_restart() {
  std::cout << t("restarting") << "\n" << std::flush;
  const int old = live_watch_pid();
  if (systemd_unit_present()) {
    systemd_user("daemon-reload", true);
    int rc = systemd_user("restart", true);
    if (rc != 0) rc = systemd_user("start", true);
    if (rc == 0) {
      using namespace std::chrono_literals;
      for (int i = 0; i < 40; ++i) {
        if (systemd_user("is-active", true) == 0) break;
        std::this_thread::sleep_for(100ms);
      }
      if (systemd_user("is-active", true) == 0) {
        const int pid = wait_new_watch_pid(old, 4000);
        std::cout << t("restart_ok") << (pid > 0 ? pid : 0) << ")\n";
        return 0;
      }
    }
    std::cerr << t("restart_systemd_fail") << "\n";
  }
  if (old) stop_watch_pid(old);
  if (spawn_watch() != 0) {
    std::cerr << t("restart_fail") << "\n";
    return 1;
  }
  const int pid = wait_new_watch_pid(old, 4000);
  if (pid <= 0) {
    std::cerr << t("restart_fail") << "\n";
    return 1;
  }
  std::cout << t("restart_ok") << pid << ")\n";
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
  std::cout << t("restart_hint") << "\n";
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
  if (cmd == "lutris" || cmd == "lutris-spil") return cmd_lutris();
  if (cmd == "lutris-install" || cmd == "lutris-installer") return cmd_lutris_install(arg);
  if (cmd == "emu" || cmd == "emu-spil" || cmd == "roms") return cmd_emu();
  if (cmd == "scan") return cmd_scan(arg);
  if (cmd == "restart" || cmd == "genstart") return cmd_restart();
  if (cmd == "startwatch" || cmd == "lyt") return cmd_start_watch();
  if (cmd == "watch" || cmd == "run") {
    if (arg == "restart" || arg == "genstart") return cmd_restart();
    nfc_watch_arm();
    return cmd_watch();
  }
  if (cmd == "start" || cmd == "play" || cmd == "launch") return cmd_start(arg);
  if (cmd == "stop" || cmd == "luk") return cmd_stop(arg);
  if (cmd == "lock" || cmd == "laas" || cmd == "lås") return cmd_lock();
  if (cmd == "read" || cmd == "læs" || cmd == "laes") return cmd_read();
  if (cmd == "write" || cmd == "skriv") return cmd_write();
  if (cmd == "add" || cmd == "tilfoj" || cmd == "tilføj") return cmd_add(arg);
  if (cmd == "remove" || cmd == "rm" || cmd == "slet") return cmd_remove(arg);
  if (cmd == "tag" || cmd == "brik") {
    const auto sp = arg.find(' ');
    const std::string sub = sp == std::string::npos ? arg : arg.substr(0, sp);
    const std::string rest = sp == std::string::npos ? "" : trim_arg(arg.substr(sp + 1));
    if (sub == "backup" || sub == "sikkerhedskopi" || sub == "dump") return cmd_tag_backup(rest);
    if (sub == "restore" || sub == "gendan" || sub == "genopret") return cmd_tag_restore(rest);
    std::cerr << t("tag_usage") << "\n";
    return 2;
  }
  if (cmd == "sprog" || cmd == "lang" || cmd == "language") return cmd_lang(arg);
  if (cmd == "udev") return cmd_udev(arg);
  if (cmd == "install" || cmd == "installer") return cmd_udev("install");

  const bool uses_reader = cmd == "firmware" || cmd == "led" || cmd == "beep" ||
                           cmd == "demo" || cmd == "colors" || cmd == "farver" ||
                           cmd == "reset";
  if (!uses_reader) {
    usage();
    return 2;
  }
  if (cmd == "led" && arg.empty()) {
    usage();
    return 2;
  }
  UsbPause pause;
  auto reader = open_reader();
  if (cmd == "reset") {
    reader.reset_hw();
    std::cout << "Hardware reset sent\n";
  } else if (cmd == "firmware") {
    std::cout << reader.firmware() << "\n";
  } else if (cmd == "led") {
    reader.set_led(parse_led(arg));
  } else if (cmd == "beep") {
    int ms = 200;
    if (!arg.empty()) ms = std::atoi(arg.c_str());
    if (ms < 100) ms = 100;
    reader.beep(std::chrono::milliseconds{ms});
  } else if (cmd == "demo" || cmd == "colors" || cmd == "farver") {
    demo(reader);
  }
  if (cmd != "led") led_not_listening(reader);
  return 0;
}
