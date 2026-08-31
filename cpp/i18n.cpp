#include "i18n.hpp"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <pwd.h>
#include <string>
#include <unistd.h>
#include <unordered_map>
#include <vector>

namespace {

std::string g_code = "da";
std::unordered_map<std::string, std::string> g_msg;
std::unordered_map<std::string, std::string> g_fallback;
std::vector<Language> g_langs;

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

std::string unescape(std::string s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '\\' && i + 1 < s.size()) {
      const char n = s[++i];
      if (n == 'n') out.push_back('\n');
      else if (n == 't') out.push_back('\t');
      else out.push_back(n);
    } else {
      out.push_back(s[i]);
    }
  }
  return out;
}

std::vector<std::filesystem::path> lang_dirs() {
  std::vector<std::filesystem::path> dirs;
  auto add = [&](std::filesystem::path p) {
    if (!p.empty()) dirs.push_back(std::move(p));
  };
  if (const char* share = std::getenv("NFC_SHARE"); share && *share) {
    add(std::filesystem::path(share) / "lang");
    add(share);
  }
  add(std::filesystem::current_path() / "lang");
  add(std::filesystem::current_path());
  char buf[4096];
  const ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
  if (n > 0) {
    buf[n] = 0;
    const auto exe = std::filesystem::path(buf);
    const auto root = exe.parent_path();
    add(root / "lang");
    add(root.parent_path() / "lang");
    add(root.parent_path() / "share" / "nfc-games" / "lang");
    add(root.parent_path().parent_path() / "share" / "nfc-games" / "lang");
  }
  return dirs;
}

std::filesystem::path find_lang_file(const std::string& name) {
  for (const auto& dir : lang_dirs()) {
    const auto p = dir / name;
    std::error_code ec;
    if (std::filesystem::is_regular_file(p, ec)) return p;
  }
  return {};
}

void load_lang_file(const std::filesystem::path& path,
                    std::unordered_map<std::string, std::string>& out) {
  std::ifstream in(path);
  if (!in) return;
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty() || line[0] == '#') continue;
    const auto here = line.find("<<<");
    if (here != std::string::npos && here > 0) {
      auto key = trim(line.substr(0, here));
      std::string val;
      while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (trim(line) == "<<<") break;
        if (!val.empty()) val.push_back('\n');
        val += line;
      }
      if (!key.empty()) out[key] = std::move(val);
      continue;
    }
    const auto eq = line.find('=');
    if (eq == std::string::npos) continue;
    auto key = trim(line.substr(0, eq));
    auto val = unescape(line.substr(eq + 1));
    if (!key.empty()) out[key] = std::move(val);
  }
}

std::vector<std::pair<std::string, std::string>> g_lang_store;

void load_lang_list_ok() {
  g_langs.clear();
  g_lang_store.clear();
  const auto path = find_lang_file("langs.txt");
  if (path.empty()) {
    g_lang_store = {{"da", "Dansk"}, {"en", "English"}};
  } else {
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
      if (!line.empty() && line.back() == '\r') line.pop_back();
      line = trim(line);
      if (line.empty() || line[0] == '#') continue;
      auto sp = line.find_first_of(" \t");
      std::string code, name;
      if (sp == std::string::npos) {
        code = line;
        name = line;
      } else {
        code = trim(line.substr(0, sp));
        name = trim(line.substr(sp + 1));
      }
      if (code.empty()) continue;
      g_lang_store.emplace_back(std::move(code), std::move(name));
    }
  }
  g_langs.reserve(g_lang_store.size());
  for (const auto& p : g_lang_store) {
    g_langs.push_back({p.first.c_str(), p.second.c_str()});
  }
}

std::string normalize_code(std::string_view raw) {
  std::string s = lower(trim(std::string{raw}));
  auto dot = s.find('.');
  if (dot != std::string::npos) s = s.substr(0, dot);
  auto us = s.find('_');
  if (us != std::string::npos) s = s.substr(0, us);
  auto dash = s.find('-');
  if (dash != std::string::npos) s = s.substr(0, dash);
  if (s == "dk" || s == "dansk" || s == "danish") return "da";
  if (s == "us" || s == "gb" || s == "engelsk" || s == "english") return "en";
  if (s == "german" || s == "deutsch" || s == "tysk") return "de";
  if (s == "se" || s == "swedish" || s == "svenska" || s == "svensk") return "sv";
  if (s == "no" || s == "nn" || s == "norsk" || s == "norwegian") return "nb";
  if (s == "french" || s == "francais" || s == "français" || s == "fransk") return "fr";
  return s;
}

bool known_code(const std::string& code) {
  for (const auto& lang : languages()) {
    if (code == lang.code) return true;
  }
  return false;
}

std::string detect_system_lang() {
  const char* keys[] = {"LC_ALL", "LC_MESSAGES", "LANG", nullptr};
  for (int i = 0; keys[i]; ++i) {
    const char* v = std::getenv(keys[i]);
    if (!v || !*v) continue;
    std::string s = v;
    if (s == "C" || s == "C.UTF-8" || s == "POSIX") continue;
    auto code = normalize_code(s);
    if (known_code(code)) return code;
  }
  return {};
}

void reload_catalog() {
  g_fallback.clear();
  g_msg.clear();
  if (auto en = find_lang_file("en.txt"); !en.empty()) load_lang_file(en, g_fallback);
  if (g_code == "en") {
    g_msg = g_fallback;
    return;
  }
  if (auto cur = find_lang_file(g_code + ".txt"); !cur.empty()) {
    load_lang_file(cur, g_msg);
  }
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

std::filesystem::path nfc_config_dir() {
  if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) {
    return std::filesystem::path(xdg) / "nfc-games";
  }
  const char* home = std::getenv("HOME");
  if (!home || !*home) {
    if (passwd* pw = ::getpwuid(::getuid())) home = pw->pw_dir;
  }
  return std::filesystem::path(home && *home ? home : "/tmp") / ".config" / "nfc-games";
}

std::vector<Language> languages() {
  if (g_langs.empty()) load_lang_list_ok();
  return g_langs;
}

std::filesystem::path lang_config_path() { return nfc_config_dir() / "nfc.conf"; }

std::string current_lang_code() { return g_code; }

const char* current_lang_name() {
  for (const auto& lang : languages()) {
    if (g_code == lang.code) return lang.name;
  }
  return g_code.c_str();
}

void i18n_init() {
  load_lang_list_ok();
  g_code = "da";
  if (auto sys = detect_system_lang(); !sys.empty()) g_code = sys;
  if (auto from_file = read_config_lang(); !from_file.empty() && known_code(from_file)) {
    g_code = from_file;
  }
  if (const char* env = std::getenv("NFC_LANG"); env && *env) {
    auto code = normalize_code(env);
    if (known_code(code)) g_code = code;
  }
  reload_catalog();
}

bool set_lang(std::string_view code) {
  auto norm = normalize_code(code);
  if (!known_code(norm)) return false;
  g_code = norm;
  reload_catalog();
  auto dir = nfc_config_dir();
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
  if (!key) return "";
  auto it = g_msg.find(key);
  if (it != g_msg.end()) return it->second.c_str();
  it = g_fallback.find(key);
  if (it != g_fallback.end()) return it->second.c_str();
  return key;
}

std::string t_join(const char* key, const std::string& extra) { return std::string(t(key)) + extra; }
