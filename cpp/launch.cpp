#include "launch.hpp"
#include "i18n.hpp"
#include "steam.hpp"

#include <cstdlib>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

namespace launcher {

bool is_steam_kind(const std::string& kind) {
  return kind.empty() || kind == "steam" || kind == "shortcut";
}

std::string target_id(const Tag& tag) {
  return is_steam_kind(tag.kind) ? std::to_string(tag.appid) : tag.target;
}

std::string uri(const Tag& tag) {
  if (tag.kind == "lutris") return "lutris://rungame/" + tag.target;
  if (tag.kind == "heroic") return "heroic://launch/" + tag.target;
  SteamGame g;
  g.appid = tag.appid;
  g.kind = tag.kind;
  g.name = tag.name;
  return SteamLibrary::steam_uri(g);
}

void launch(const Tag& tag) {
  if (is_steam_kind(tag.kind)) {
    SteamGame g;
    g.appid = tag.appid;
    g.kind = tag.kind;
    g.name = tag.name;
    SteamLibrary::launch(g);
    return;
  }
  // xdg-open detaches the same way Steam launch does: double fork + setsid,
  // so the caller (watch, nfc start) returns without blocking on the UI.
  const std::string u = uri(tag);
  const pid_t pid = ::fork();
  if (pid < 0) throw std::runtime_error(t("steam_fork"));
  if (pid > 0) {
    int st = 0;
    ::waitpid(pid, &st, 0);
    return;
  }
  const pid_t child = ::fork();
  if (child < 0) ::_exit(127);
  if (child > 0) ::_exit(0);
  ::setsid();
  ::execlp("xdg-open", "xdg-open", u.c_str(), static_cast<char*>(nullptr));
  ::_exit(127);
}

}  // namespace launcher