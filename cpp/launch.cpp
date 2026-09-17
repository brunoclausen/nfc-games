#include "launch.hpp"
#include "i18n.hpp"
#include "steam.hpp"

#include <cstdlib>
#include <chrono>
#include <fcntl.h>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

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

// The grandchild reports its pid through a pipe so the caller can stop the
// whole process group later (it becomes a session leader via setsid below).
pid_t run_detached(const std::string& command) {
  int p[2];
  if (::pipe(p) != 0) return -1;
  const pid_t outer = ::fork();
  if (outer < 0) {
    ::close(p[0]);
    ::close(p[1]);
    return -1;
  }
  if (outer > 0) {
    ::close(p[1]);
    int st = 0;
    ::waitpid(outer, &st, 0);
    pid_t inner = -1;
    ssize_t n = 0;
    do {
      const ssize_t r = ::read(p[0], &inner, sizeof(inner));
      if (r <= 0) break;
      n = r;
    } while (n != static_cast<ssize_t>(sizeof(inner)));
    ::close(p[0]);
    return inner;
  }
  ::close(p[0]);
  const pid_t inner = ::fork();
  if (inner < 0) {
    ::close(p[1]);
    ::_exit(127);
  }
  if (inner > 0) {
    const ssize_t w = ::write(p[1], &inner, sizeof(inner));
    (void)w;
    ::close(p[1]);
    ::_exit(0);
  }
  ::close(p[1]);
  ::setsid();
  const int fd = ::open("/dev/null", O_RDWR);
  if (fd >= 0) {
    ::dup2(fd, STDIN_FILENO);
    ::dup2(fd, STDOUT_FILENO);
    ::dup2(fd, STDERR_FILENO);
    if (fd > 2) ::close(fd);
  }
  ::execl("/bin/sh", "sh", "-c", command.c_str(), static_cast<char*>(nullptr));
  ::_exit(127);
}

void stop_child(pid_t pid) {
  if (pid <= 0) return;
  const auto group = -pid;
  ::kill(group, SIGTERM);
  for (int i = 0; i < 33; ++i) {
    if (::kill(group, 0) != 0) break;
    std::this_thread::sleep_for(std::chrono::milliseconds{150});
  }
  if (::kill(group, 0) == 0) ::kill(group, SIGKILL);
  std::this_thread::sleep_for(std::chrono::milliseconds{200});
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
  if (is_emu_kind(tag.kind)) {
    (void)run_detached(tag.target);
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