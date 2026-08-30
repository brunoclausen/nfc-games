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
  std::string start_dir;
  std::string launch_options;
  std::string kind;  // "steam" or "shortcut"
};

struct RunningGame {
  std::uint32_t appid = 0;
  int pid = 0;
};

class SteamLibrary {
 public:
  // Discovers Steam roots, extra libraries, appmanifests and shortcuts.vdf.
  static SteamLibrary scan();

  const std::vector<SteamGame>& games() const { return games_; }
  std::optional<SteamGame> find(std::string_view name_or_appid) const;
  std::vector<SteamGame> matches(std::string_view name_or_appid) const;
  static std::string steam_uri(const SteamGame& game);
  static void launch(const SteamGame& game);
  static std::vector<RunningGame> running();
  static int stop(std::uint32_t appid = 0);

 private:
  std::vector<SteamGame> games_;
};
