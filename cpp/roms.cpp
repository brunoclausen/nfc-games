#include "roms.hpp"
#include "plat.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <sstream>

namespace fs = std::filesystem;

namespace {

std::string trim(std::string s) {
  auto is_sp = [](unsigned char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
  while (!s.empty() && is_sp(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
  while (!s.empty() && is_sp(static_cast<unsigned char>(s.back()))) s.pop_back();
  return s;
}

std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

std::string expand_vars(const std::string& in, const std::map<std::string, std::string>& vars) {
  std::string out = in;
  for (const auto& [key, val] : vars) {
    const std::string needle = "${" + key + "}";
    std::size_t pos = 0;
    while ((pos = out.find(needle, pos)) != std::string::npos) {
      out.replace(pos, needle.size(), val);
      pos += val.size();
    }
  }
  return out;
}

std::string config_home() { return plat::config_home().string(); }

std::string read_file(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return {};
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

std::string first_existing(std::initializer_list<std::string> paths) {
  std::error_code ec;
  for (const auto& p : paths) {
    if (!p.empty() && fs::is_regular_file(p, ec)) return p;
  }
  return {};
}

// Splits a JSON array into its top-level objects (brace-aware, string-aware).
std::vector<std::string> split_top_objects(const std::string& s) {
  std::vector<std::string> out;
  bool in_str = false, esc = false, started = false;
  int depth = 0;
  std::size_t start = 0;
  for (std::size_t i = 0; i < s.size(); ++i) {
    const char c = s[i];
    if (in_str) {
      if (esc) {
        esc = false;
      } else if (c == '\\') {
        esc = true;
      } else if (c == '"') {
        in_str = false;
      }
      continue;
    }
    if (c == '"') {
      in_str = true;
    } else if (c == '{') {
      if (depth == 0) {
        start = i;
        started = true;
      }
      ++depth;
    } else if (c == '}') {
      if (depth > 0) {
        --depth;
        if (depth == 0 && started) {
          out.push_back(s.substr(start, i - start + 1));
          started = false;
        }
      }
    }
  }
  return out;
}

void append_utf8(std::string& out, unsigned cp) {
  if (cp < 0x80) {
    out.push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

std::optional<std::string> json_string(const std::string& s, const std::string& key) {
  const std::string needle = "\"" + key + "\"";
  std::size_t p = s.find(needle);
  if (p == std::string::npos) return std::nullopt;
  p = s.find(':', p + needle.size());
  if (p == std::string::npos) return std::nullopt;
  while (++p < s.size() && std::isspace(static_cast<unsigned char>(s[p]))) {}
  if (p >= s.size() || s[p] != '"') return std::nullopt;
  ++p;
  std::string out;
  while (p < s.size()) {
    const char c = s[p++];
    if (c == '"') break;
    if (c != '\\') {
      out.push_back(c);
      continue;
    }
    if (p >= s.size()) break;
    const char e = s[p++];
    switch (e) {
      case 'n': out.push_back('\n'); break;
      case 't': out.push_back('\t'); break;
      case 'r': out.push_back('\r'); break;
      case 'b': out.push_back('\b'); break;
      case 'f': out.push_back('\f'); break;
      case '"': out.push_back('"'); break;
      case '\\': out.push_back('\\'); break;
      case '/': out.push_back('/'); break;
      case 'u': {
        if (p + 4 > s.size()) break;
        unsigned cp = 0;
        for (int i = 0; i < 4; ++i) {
          const char h = s[p + static_cast<std::size_t>(i)];
          cp <<= 4;
          if (h >= '0' && h <= '9') cp |= static_cast<unsigned>(h - '0');
          else if (h >= 'a' && h <= 'f') cp |= static_cast<unsigned>(h - 'a' + 10);
          else if (h >= 'A' && h <= 'F') cp |= static_cast<unsigned>(h - 'A' + 10);
        }
        p += 4;
        append_utf8(out, cp);
        break;
      }
      default: out.push_back(e); break;
    }
  }
  return out;
}

std::optional<bool> json_bool(const std::string& s, const std::string& key) {
  const std::string needle = "\"" + key + "\"";
  std::size_t p = s.find(needle);
  if (p == std::string::npos) return std::nullopt;
  p = s.find(':', p + needle.size());
  if (p == std::string::npos) return std::nullopt;
  ++p;
  while (p < s.size() && std::isspace(static_cast<unsigned char>(s[p]))) ++p;
  if (s.compare(p, 4, "true") == 0) return true;
  if (s.compare(p, 5, "false") == 0) return false;
  return std::nullopt;
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

std::string dquote_escape(const std::string& s) {
  std::string out;
  for (char c : s) {
    if (c == '\\' || c == '"' || c == '$' || c == '`') out.push_back('\\');
    out.push_back(c);
  }
  return out;
}

void erase_all(std::string& s, const std::string& needle) {
  if (needle.empty()) return;
  std::size_t p = 0;
  while ((p = s.find(needle, p)) != std::string::npos) s.erase(p, needle.size());
}

const char* emu_os_name() {
#if defined(__APPLE__)
  return "mac";
#elif defined(_WIN32)
  return "win";
#else
  return "linux";
#endif
}

std::string default_cores_dir() {
  std::error_code ec;
  if (const char* home = std::getenv("HOME"); home && *home) {
    const std::string flatpak =
        std::string(home) + "/.var/app/org.libretro.RetroArch/config/retroarch/cores";
    if (fs::is_directory(flatpak, ec)) return flatpak;
  }
  const std::string cfg = config_home();
  if (!cfg.empty()) {
    const std::string ra = cfg + "/retroarch/cores";
    if (fs::is_directory(ra, ec)) return ra;
  }
  return {};
}

std::vector<std::string> split_macro_levels(const std::string& s, char sep) {
  std::vector<std::string> out;
  std::string cur;
  std::size_t depth = 0;
  for (std::size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '$' && i + 1 < s.size() && s[i + 1] == '{') {
      ++depth;
      cur += s[i];
      cur += s[i + 1];
      ++i;
      continue;
    }
    if (s[i] == '}') {
      if (depth) --depth;
      cur += s[i];
      continue;
    }
    if (s[i] == sep && depth == 0) {
      out.push_back(cur);
      cur.clear();
      continue;
    }
    cur += s[i];
  }
  out.push_back(cur);
  return out;
}

std::string expand_emu_macros(const std::string& s, const std::string& exec,
                              const std::string& racores) {
  std::string out;
  std::size_t i = 0;
  while (i < s.size()) {
    const std::size_t open = s.find("${", i);
    if (open == std::string::npos) {
      out += s.substr(i);
      break;
    }
    out += s.substr(i, open - i);
    std::size_t depth = 1;
    std::size_t j = open + 2;
    for (; j < s.size() && depth; ++j) {
      if (s[j] == '$' && j + 1 < s.size() && s[j + 1] == '{') {
        ++depth;
        ++j;
      } else if (s[j] == '}') {
        --depth;
      }
    }
    const std::string inner = s.substr(open + 2, j - open - 3);
    const std::size_t colon = inner.find(':');
    const std::string name = colon == std::string::npos ? inner : inner.substr(0, colon);
    const std::string rest = colon == std::string::npos ? "" : inner.substr(colon + 1);
    if (name == "filePath") {
      // Substituted per-game further down.
      out += "${filePath}";
    } else if (name == "os") {
      const std::vector<std::string> parts = split_macro_levels(rest, '|');
      const bool hit = !parts.empty() && parts[0] == emu_os_name();
      const std::string chosen = hit ? (parts.size() > 1 ? parts[1] : "")
                                     : (parts.size() > 2 ? parts[2] : "");
      out += expand_emu_macros(chosen, exec, racores);
    } else if (name == "/") {
      out += "/";
    } else if (name == "racores") {
      out += racores;
    } else if (name == "exepath" || name == "exe") {
      out += exec;
    } else {
      // Unknown macro -> drop the token entirely.
    }
    i = j;
  }
  return out;
}

std::string remove_leftover_macros(const std::string& s) {
  std::string out;
  std::size_t i = 0;
  while (i < s.size()) {
    const std::size_t open = s.find("${", i);
    if (open == std::string::npos) {
      out += s.substr(i);
      break;
    }
    out += s.substr(i, open - i);
    std::size_t depth = 1;
    std::size_t j = open + 2;
    for (; j < s.size() && depth; ++j) {
      if (s[j] == '$' && j + 1 < s.size() && s[j + 1] == '{') {
        ++depth;
        ++j;
      } else if (s[j] == '}') {
        --depth;
      }
    }
    i = j;
  }
  return out;
}

std::string srm_config_path() {
  if (const char* e = std::getenv("NFC_SRM_CONFIG"); e && *e) return e;
  const std::string cfg = config_home();
  if (cfg.empty()) return {};
  return first_existing({cfg + "/steam-rom-manager/userData/userConfigurations.json",
                         cfg + "/EmuDeck/backend/configs/steam-rom-manager/userData/"
                                "userConfigurations.json"});
}

std::string user_settings_path() {
  const std::string cfg = srm_config_path();
  if (cfg.empty()) return {};
  return (fs::path(cfg).parent_path() / "userSettings.json").string();
}

std::string roms_dir_path() {
  if (const char* e = std::getenv("NFC_ROMS_DIR"); e && *e) return e;
  const std::string settings = user_settings_path();
  if (!settings.empty()) {
    if (auto dir = json_string(read_file(settings), "romsDirectory"); dir && !dir->empty()) {
      return *dir;
    }
  }
  const auto home = plat::home_dir();
  if (!home.empty()) return (home / "Emulation" / "roms").string();
  return {};
}

std::map<std::string, std::string> config_vars(const std::string& roms_dir) {
  std::map<std::string, std::string> vars;
  vars["romsdirglobal"] = roms_dir;
  vars["/"] = "/";
  const std::string settings = user_settings_path();
  if (!settings.empty()) {
    const std::string text = read_file(settings);
    if (auto v = json_string(text, "retroarchPath"); v && !v->empty()) vars["retroarchpath"] = *v;
    if (auto v = json_string(text, "steamDirectory"); v && !v->empty()) vars["steamdirglobal"] = *v;
    if (auto v = json_string(text, "raCoresDirectory"); v && !v->empty()) vars["racores"] = *v;
  }
  if (vars.find("racores") == vars.end()) vars["racores"] = default_cores_dir();
  return vars;
}

std::string strip_dup_ext(std::string name) {
  for (const std::string suffix : {".nkit", ".xiso"}) {
    if (name.size() > suffix.size() &&
        lower(name.substr(name.size() - suffix.size())) == suffix) {
      name.erase(name.size() - suffix.size());
    }
  }
  return name;
}

bool has_ext(const std::vector<std::string>& exts, const std::string& ext) {
  return std::find(exts.begin(), exts.end(), ext) != exts.end();
}

}  // namespace

std::vector<std::string> glob_extensions(const std::string& glob) {
  std::vector<std::string> out;
  const std::size_t at = glob.find("@(");
  if (at == std::string::npos) return out;
  const std::size_t start = at + 2;
  const std::size_t end = glob.find(')', start);
  const std::string list = glob.substr(start, end == std::string::npos ? std::string::npos : end - start);
  std::size_t pos = 0;
  while (pos <= list.size()) {
    const std::size_t bar = list.find('|', pos);
    std::string ext = lower(trim(list.substr(pos, bar == std::string::npos ? std::string::npos
                                                                           : bar - pos)));
    if (!ext.empty()) {
      if (ext.front() != '.') ext.insert(ext.begin(), '.');
      if (has_ext(out, ext) == false) out.push_back(ext);
    }
    if (bar == std::string::npos) break;
    pos = bar + 1;
  }
  return out;
}

std::string build_emu_command(const std::string& launcher, const std::string& args,
                              const std::string& file_path, const std::string& racores) {
  std::string a = args;
  erase_all(a, "%command%");
  erase_all(a, "vblank_mode=0");
  a = expand_emu_macros(trim(a), launcher, racores);
  a = trim(a);
  const std::string exe =
      launcher.find_first_of(" \t\"'") == std::string::npos ? launcher : shell_single_quote(launcher);
  if (a.find("${filePath}") != std::string::npos) {
    std::string rest = a;
    erase_all(rest, "${filePath}");
    rest = remove_leftover_macros(rest);
    if (rest.find("${") == std::string::npos && rest.find('%') == std::string::npos) {
      const std::string needle = "${filePath}";
      std::string filled;
      filled.reserve(a.size() + file_path.size());
      bool in_dq = false;
      for (std::size_t i = 0; i < a.size();) {
        if (a.compare(i, needle.size(), needle) == 0) {
          // Inside "..." the path is escaped for that context (the surrounding
          // quotes are kept); bare, it is single-quoted so spaces survive.
          filled += in_dq ? dquote_escape(file_path) : shell_single_quote(file_path);
          i += needle.size();
          continue;
        }
        if (a[i] == '"') in_dq = !in_dq;
        filled.push_back(a[i]);
        ++i;
      }
      return exe + " " + filled;
    }
  }
  if (!a.empty()) return exe + " " + remove_leftover_macros(a);
  return exe + " " + shell_single_quote(file_path);
}

std::vector<SrmParser> parse_srm_config(const std::string& json, const std::string& roms_dir) {
  std::vector<SrmParser> out;
  const std::map<std::string, std::string> vars = {{"romsdirglobal", roms_dir}, {"/", "/"}};
  for (const std::string& obj : split_top_objects(json)) {
    SrmParser p;
    p.title = json_string(obj, "configTitle").value_or("");
    p.rom_dir = expand_vars(json_string(obj, "romDirectory").value_or(""), vars);
    p.launcher = expand_vars(json_string(obj, "path").value_or(""), vars);
    p.args = json_string(obj, "executableArgs").value_or("");
    p.disabled = json_bool(obj, "disabled").value_or(false);
    if (auto glob = json_string(obj, "glob")) p.exts = glob_extensions(*glob);
    if (p.launcher.empty() || p.rom_dir.empty()) continue;
    out.push_back(std::move(p));
  }
  return out;
}

bool EmuLibrary::available() {
  const std::string cfg = srm_config_path();
  if (cfg.empty()) return false;
  std::error_code ec;
  return fs::is_directory(roms_dir_path(), ec);
}

EmuLibrary EmuLibrary::scan() {
  EmuLibrary lib;
  const std::string cfg = srm_config_path();
  if (cfg.empty()) return lib;
  const std::string roms_dir = roms_dir_path();
  if (roms_dir.empty()) return lib;
  const std::map<std::string, std::string> vars = config_vars(roms_dir);
  std::vector<SrmParser> parsers = parse_srm_config(read_file(cfg), roms_dir);
  for (SrmParser& p : parsers) {
    p.launcher = expand_vars(p.launcher, vars);
    p.args = expand_vars(p.args, vars);
  }
  // A system can have several parsers (e.g. Switch: Citron, Eden, Ryujinx) and
  // only one of them installed. Prefer parsers whose launcher exists, so the
  // duplicate files bind to the emulator that is actually there.
  std::stable_partition(parsers.begin(), parsers.end(), [](const SrmParser& p) {
    if (p.launcher.find("${") != std::string::npos) return false;
    if (p.launcher.find('/') == std::string::npos) return true;
    std::error_code e;
    return fs::exists(p.launcher, e);
  });

  std::set<std::string> seen;
  std::error_code ec;
  for (const SrmParser& p : parsers) {
    if (p.disabled || p.exts.empty()) continue;
    if (p.launcher.find("${") != std::string::npos) continue;
    if (!fs::is_directory(p.rom_dir, ec)) continue;
    fs::recursive_directory_iterator it(p.rom_dir, fs::directory_options::skip_permission_denied, ec);
    while (!ec && it != fs::recursive_directory_iterator()) {
      std::error_code fec;
      if (!it->is_regular_file(fec)) {
        it.increment(ec);
        continue;
      }
      const fs::path path = it->path();
      if (!has_ext(p.exts, lower(path.extension().string()))) {
        it.increment(ec);
        continue;
      }
      const std::string full = path.string();
      if (!seen.insert(full).second) {
        it.increment(ec);
        continue;
      }
      EmuGame g;
      g.title = p.title;
      const fs::path rel = fs::relative(path, roms_dir, ec);
      if (rel.empty() || ec) {
        ec.clear();
        g.system = path.parent_path().filename().string();
      } else {
        g.system = rel.begin()->string();
      }
      g.name = strip_dup_ext(path.stem().string());
      g.path = full;
      g.command = build_emu_command(p.launcher, p.args, full, vars.at("racores"));
      const std::string key = lower(g.system) + "|" + lower(g.name);
      if (seen.insert(key).second) lib.games_.push_back(std::move(g));
      it.increment(ec);
    }
  }
  std::sort(lib.games_.begin(), lib.games_.end(), [](const EmuGame& a, const EmuGame& b) {
    if (a.system != b.system) return a.system < b.system;
    return lower(a.name) < lower(b.name);
  });
  return lib;
}
