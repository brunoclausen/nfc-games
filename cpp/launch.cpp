#include "launch.hpp"
#include "plat.hpp"
#include "steam.hpp"

#include <string>

namespace launcher {

bool is_steam_kind(const std::string& kind) {
  return kind.empty() || kind == "steam" || kind == "shortcut";
}

bool is_emu_kind(const std::string& kind) { return kind == "emu"; }

std::string target_id(const Tag& tag) {
  return is_steam_kind(tag.kind) ? std::to_string(tag.appid) : tag.target;
}

std::string uri(const Tag& tag) {
  if (tag.kind == "lutris") return "lutris://rungame/" + tag.target;
  if (tag.kind == "heroic") return "heroic://launch/" + tag.target;
  if (is_emu_kind(tag.kind)) return tag.target;  // it is already a shell command
  SteamGame g;
  g.appid = tag.appid;
  g.kind = tag.kind;
  g.name = tag.name;
  return SteamLibrary::steam_uri(g);
}

pid_t run_detached(const std::string& command) { return plat::spawn_shell(command); }

void stop_child(pid_t pid) { plat::stop_shell(pid); }

void launch(const Tag& tag) {
  if (is_steam_kind(tag.kind)) {
    SteamGame g;
    g.appid = tag.appid;
    g.kind = tag.kind;
    g.name = tag.name;
    SteamLibrary::launch(g);
    return;
  }
  if (is_emu_kind(tag.kind)) {
    (void)run_detached(tag.target);
    return;
  }
  plat::open_uri(uri(tag));
}

}  // namespace launcher
