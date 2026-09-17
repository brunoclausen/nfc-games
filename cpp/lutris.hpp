#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct LutrisGame {
  std::string name;
  std::string slug;
  std::string runner;
};

// Installed Lutris games, read from `lutris -l -o`. Steam-runner games are
// left out; those belong to the Steam list and have an automatic stop.
class LutrisLibrary {
 public:
  static bool available();
  static LutrisLibrary scan();

  const std::vector<LutrisGame>& games() const { return games_; }
  std::optional<LutrisGame> find(std::string_view name_or_slug) const;

 private:
  std::vector<LutrisGame> games_;
};

// Parses the pipe-separated `lutris -l -o` output (also used by tests).
std::vector<LutrisGame> parse_lutris_list(const std::string& text);
