#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct LutrisGame {
  std::string name;
  std::string slug;
  std::string runner;
  std::string install_path;  // "-" or "false" if not installed, path if installed
  bool installed() const { return install_path != "-" && install_path != "false" && !install_path.empty(); }
};

// Installed Lutris games, read from `lutris -l`. Steam-runner games are
// left out; those belong to the Steam list and have an automatic stop.
class LutrisLibrary {
  public:
    static bool available();
    static LutrisLibrary scan();
    static LutrisLibrary scan_gog();  // all GOG games from service database
    static std::string find_binary();

    const std::vector<LutrisGame>& games() const { return games_; }
    std::optional<LutrisGame> find(std::string_view name_or_slug) const;

  private:
    std::vector<LutrisGame> games_;
};

// Parses the pipe-separated `lutris -l` output (also used by tests).
std::vector<LutrisGame> parse_lutris_list(const std::string& text);

// Parses `lutris --list-service-games gog` output.
std::vector<LutrisGame> parse_lutris_service_list(const std::string& text);
