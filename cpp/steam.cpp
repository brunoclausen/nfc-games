#include "steam.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

std::string home_dir() {
  if (const char* h = std::getenv("HOME")) return h;
  return "/var/home/bruno";
}

bool is_tool(const SteamGame& g) {
  const std::string n = lower(g.name);
  if (n.find("proton") == 0) return true;
  if (n.find("steam linux runtime") == 0) return true;
  if (n.find("steamworks common") == 0) return true;
  if (g.appid == 228980) return true;
  return false;
}

std::optional<std::string> read_file(const fs::path& p) {
  std::ifstream in(p);
  if (!in) return std::nullopt;
  std::ostringstream os;
  os << in.rdbuf();
  return os.str();
}

std::vector<std::string> vdf_all_strings(const std::string& text, std::string_view key) {
  std::vector<std::string> out;
  const std::string pat = "\"" + std::string(key) + "\"";
  std::size_t i = 0;
  while ((i = text.find(pat, i)) != std::string::npos) {
    i += pat.size();
    auto q1 = text.find('"', i);
    if (q1 == std::string::npos) break;
    auto q2 = text.find('"', q1 + 1);
    if (q2 == std::string::npos) break;
    out.push_back(text.substr(q1 + 1, q2 - q1 - 1));
    i = q2 + 1;
  }
  return out;
}

std::optional<std::string> vdf_string(const std::string& text, std::string_view key) {
  auto all = vdf_all_strings(text, key);
  if (all.empty()) return std::nullopt;
  return all.front();
}

std::vector<fs::path> steam_roots() {
  const fs::path home = home_dir();
  std::vector<fs::path> cands = {
      home / ".steam" / "steam",
      home / ".local" / "share" / "Steam",
      home / ".var" / "app" / "com.valvesoftware.Steam" / "data" / "Steam",
  };
  if (const char* env = std::getenv("STEAM_DIR")) cands.insert(cands.begin(), fs::path(env));

  std::vector<fs::path> roots;
  std::unordered_set<std::string> seen;
  for (auto p : cands) {
    std::error_code ec;
    if (!fs::exists(p, ec)) continue;
    auto canon = fs::weakly_canonical(p, ec);
    if (ec) canon = p;
    const std::string key = canon.string();
    if (!seen.insert(key).second) continue;
    roots.push_back(canon);
  }
  return roots;
}

std::vector<fs::path> library_paths(const std::vector<fs::path>& roots) {
  std::vector<fs::path> libs;
  std::unordered_set<std::string> seen;
  auto add = [&](fs::path p) {
    std::error_code ec;
    if (!fs::exists(p, ec)) return;
    auto canon = fs::weakly_canonical(p, ec);
    if (ec) canon = p;
    if (seen.insert(canon.string()).second) libs.push_back(canon);
  };
  for (const auto& root : roots) {
    add(root);
    for (const auto& rel : {fs::path("steamapps") / "libraryfolders.vdf",
                            fs::path("config") / "libraryfolders.vdf"}) {
      auto text = read_file(root / rel);
      if (!text) continue;
      for (const auto& path : vdf_all_strings(*text, "path")) add(path);
    }
  }
  return libs;
}

std::optional<SteamGame> parse_manifest(const fs::path& acf) {
  auto text = read_file(acf);
  if (!text) return std::nullopt;
  auto name = vdf_string(*text, "name");
  auto appid_s = vdf_string(*text, "appid");
  if (!name || !appid_s || name->empty()) return std::nullopt;
  SteamGame g;
  g.kind = "steam";
  g.name = *name;
  try {
    g.appid = static_cast<std::uint32_t>(std::stoul(*appid_s));
  } catch (...) {
    return std::nullopt;
  }
  return g;
}

bool read_cstr(const std::vector<std::uint8_t>& data, std::size_t& pos, std::string& out) {
  out.clear();
  while (pos < data.size()) {
    char c = static_cast<char>(data[pos++]);
    if (c == 0) return true;
    out.push_back(c);
  }
  return false;
}

struct Bvdf {
  std::string str;
  std::uint32_t num = 0;
  bool is_int = false;
  std::vector<std::pair<std::string, Bvdf>> kids;
  const Bvdf* child(std::string_view key) const {
    for (const auto& [k, v] : kids) {
      if (lower(k) == lower(std::string(key))) return &v;
    }
    return nullptr;
  }
  std::string s(std::string_view key) const {
    if (auto* c = child(key)) return c->str;
    return {};
  }
  std::uint32_t u32(std::string_view key) const {
    if (auto* c = child(key); c && c->is_int) return c->num;
    return 0;
  }
};

bool parse_bvdf_object(const std::vector<std::uint8_t>& data, std::size_t& pos, Bvdf& obj) {
  while (pos < data.size()) {
    const std::uint8_t type = data[pos++];
    if (type == 0x08) return true;
    std::string key;
    if (!read_cstr(data, pos, key)) return false;
    Bvdf node;
    if (type == 0x00) {
      if (!parse_bvdf_object(data, pos, node)) return false;
    } else if (type == 0x01) {
      if (!read_cstr(data, pos, node.str)) return false;
    } else if (type == 0x02) {
      if (pos + 4 > data.size()) return false;
      node.is_int = true;
      node.num = static_cast<std::uint32_t>(data[pos]) |
                 (static_cast<std::uint32_t>(data[pos + 1]) << 8) |
                 (static_cast<std::uint32_t>(data[pos + 2]) << 16) |
                 (static_cast<std::uint32_t>(data[pos + 3]) << 24);
      pos += 4;
    } else if (type == 0x07) {
      if (pos + 8 > data.size()) return false;
      pos += 8;
    } else {
      return false;
    }
    obj.kids.emplace_back(std::move(key), std::move(node));
  }
  return true;
}

std::vector<SteamGame> parse_shortcuts(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return {};
  std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(in)),
                                 std::istreambuf_iterator<char>());
  if (data.size() < 2) return {};
  std::size_t pos = 0;
  Bvdf root;
  if (data[0] == 0x00) {
    pos = 1;
    std::string root_key;
    if (!read_cstr(data, pos, root_key)) return {};
    if (!parse_bvdf_object(data, pos, root)) return {};
  } else {
    return {};
  }
  std::vector<SteamGame> out;
  for (const auto& [idx, entry] : root.kids) {
    SteamGame g;
    g.kind = "shortcut";
    g.name = entry.s("appname");
    if (g.name.empty()) g.name = entry.s("AppName");
    g.exe = entry.s("exe");
    g.appid = entry.u32("appid");
    if (g.name.empty()) continue;
    out.push_back(std::move(g));
  }
  return out;
}

}  // namespace

SteamLibrary SteamLibrary::scan() {
  SteamLibrary lib;
  std::unordered_set<std::uint32_t> seen_ids;
  std::unordered_set<std::string> seen_names;
  auto add = [&](SteamGame g) {
    if (g.name.empty() || is_tool(g)) return;
    if (g.appid != 0 && !seen_ids.insert(g.appid).second) return;
    const std::string nk = lower(g.name) + "|" + lower(g.exe);
    if (g.appid == 0 && !seen_names.insert(nk).second) return;
    lib.games_.push_back(std::move(g));
  };

  const auto roots = steam_roots();
  for (const auto& library : library_paths(roots)) {
    const fs::path apps = library / "steamapps";
    std::error_code ec;
    if (!fs::exists(apps, ec)) continue;
    for (const auto& ent : fs::directory_iterator(apps, ec)) {
      if (ec) break;
      const auto name = ent.path().filename().string();
      if (name.rfind("appmanifest_", 0) != 0 || ent.path().extension() != ".acf") continue;
      if (auto g = parse_manifest(ent.path())) add(*g);
    }
  }
  for (const auto& root : roots) {
    const fs::path userdata = root / "userdata";
    std::error_code ec;
    if (!fs::exists(userdata, ec)) continue;
    for (const auto& user : fs::directory_iterator(userdata, ec)) {
      if (ec || !user.is_directory()) continue;
      auto shortcuts = user.path() / "config" / "shortcuts.vdf";
      for (auto& g : parse_shortcuts(shortcuts)) add(g);
    }
  }

  std::sort(lib.games_.begin(), lib.games_.end(), [](const SteamGame& a, const SteamGame& b) {
    return lower(a.name) < lower(b.name);
  });
  return lib;
}

std::vector<SteamGame> SteamLibrary::matches(std::string_view name_or_appid) const {
  const std::string q = lower(std::string(name_or_appid));
  std::vector<SteamGame> out;
  if (q.empty()) return out;
  for (const auto& g : games_) {
    if (std::to_string(g.appid) == q) {
      out.push_back(g);
      return out;
    }
  }
  for (const auto& g : games_) {
    if (lower(g.name) == q) out.push_back(g);
  }
  if (!out.empty()) return out;
  for (const auto& g : games_) {
    if (lower(g.name).find(q) != std::string::npos) out.push_back(g);
  }
  return out;
}

std::optional<SteamGame> SteamLibrary::find(std::string_view name_or_appid) const {
  auto hit = matches(name_or_appid);
  if (hit.size() == 1) return hit.front();
  return std::nullopt;
}
