#include "lutris.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <unistd.h>

namespace {

std::string trim(std::string s) {
  auto is_sp = [](unsigned char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
  while (!s.empty() && is_sp(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
  while (!s.empty() && is_sp(static_cast<unsigned char>(s.back()))) s.pop_back();
  return s;
}

std::string find_lutris_binary() {
  const char* path = std::getenv("PATH");
  if (!path || !*path) return {};
  const std::string p(path);
  std::size_t start = 0;
  while (start <= p.size()) {
    const std::size_t end = p.find(':', start);
    const std::string dir =
        p.substr(start, end == std::string::npos ? std::string::npos : end - start);
    if (!dir.empty()) {
      const std::string cand = dir + "/lutris";
      if (::access(cand.c_str(), X_OK) == 0) return cand;
    }
    if (end == std::string::npos) break;
    start = end + 1;
  }
  return {};
}

std::vector<std::string> split_pipe(const std::string& line) {
  std::vector<std::string> out;
  std::size_t start = 0;
  while (true) {
    const std::size_t pos = line.find('|', start);
    if (pos == std::string::npos) {
      out.push_back(line.substr(start));
      break;
    }
    out.push_back(line.substr(start, pos - start));
    start = pos + 1;
  }
  return out;
}

}  // namespace

std::vector<LutrisGame> parse_lutris_list(const std::string& text) {
  std::vector<LutrisGame> games;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    if (line.find('|') == std::string::npos) continue;
    const auto cols = split_pipe(line);
    if (cols.size() < 4) continue;
    LutrisGame g;
    g.name = trim(cols[1]);
    g.slug = trim(cols[2]);
    g.runner = trim(cols[3]);
    g.install_path = cols.size() >= 5 ? trim(cols[4]) : "";
    if (g.name.empty() || g.slug.empty()) continue;
    if (g.runner == "steam") continue;  // those live in the Steam list
    games.push_back(std::move(g));
  }
  std::sort(games.begin(), games.end(),
            [](const LutrisGame& a, const LutrisGame& b) { return a.name < b.name; });
  return games;
}

std::vector<LutrisGame> parse_lutris_service_list(const std::string& text) {
  std::vector<LutrisGame> games;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    if (line.find('|') == std::string::npos) continue;
    const auto cols = split_pipe(line);
    if (cols.size() < 4) continue;
    LutrisGame g;
    g.name = trim(cols[1]);
    g.slug = trim(cols[2]);
    g.runner = trim(cols[3]);
    g.install_path = cols.size() >= 5 ? trim(cols[4]) : "";
    if (g.name.empty() || g.slug.empty()) continue;
    games.push_back(std::move(g));
  }
  std::sort(games.begin(), games.end(),
            [](const LutrisGame& a, const LutrisGame& b) { return a.name < b.name; });
  return games;
}

bool LutrisLibrary::available() { return !find_lutris_binary().empty(); }

std::string LutrisLibrary::find_binary() { return find_lutris_binary(); }

LutrisLibrary LutrisLibrary::scan() {
  LutrisLibrary lib;
  const std::string bin = find_lutris_binary();
  if (bin.empty()) return lib;
  const std::string cmd = "\"" + bin + "\" -l 2>/dev/null";
  FILE* f = ::popen(cmd.c_str(), "r");
  if (!f) return lib;
  std::string out;
  std::array<char, 4096> buf{};
  std::size_t n = 0;
  while ((n = std::fread(buf.data(), 1, buf.size(), f)) > 0) out.append(buf.data(), n);
  ::pclose(f);
  lib.games_ = parse_lutris_list(out);
  return lib;
}

LutrisLibrary LutrisLibrary::scan_gog() {
  LutrisLibrary lib;
  const std::string bin = find_lutris_binary();
  if (bin.empty()) return lib;
  const std::string cmd = "\"" + bin + "\" --list-service-games gog 2>/dev/null";
  FILE* f = ::popen(cmd.c_str(), "r");
  if (!f) return lib;
  std::string out;
  std::array<char, 4096> buf{};
  std::size_t n = 0;
  while ((n = std::fread(buf.data(), 1, buf.size(), f)) > 0) out.append(buf.data(), n);
  ::pclose(f);
  lib.games_ = parse_lutris_service_list(out);
  return lib;
}

std::optional<LutrisGame> LutrisLibrary::find(std::string_view query) const {
  for (const auto& g : games_) {
    if (g.slug == query || g.name == query) return g;
  }
  std::string needle(query);
  std::transform(needle.begin(), needle.end(), needle.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  for (const auto& g : games_) {
    std::string n = g.name;
    std::transform(n.begin(), n.end(), n.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (n == needle) return g;
  }
  return std::nullopt;
}
