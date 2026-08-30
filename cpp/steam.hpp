#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct SteamGame {
  std::uint32_t appid = 0;
  std::string name;
  std::string exe;
  std::string kind;  // "steam" or "shortcut"
};

class SteamLibrary {
 public:
  // Discovers Steam roots, extra libraries, appmanifests and shortcuts.vdf.
  static SteamLibrary scan();

  const std::vector<SteamGame>& games() const { return games_; }
  std::optional<SteamGame> find(std::string_view name_or_appid) const;

 private:
  std::vector<SteamGame> games_;
};
