#pragma once

#include "tags.hpp"

#include <sys/types.h>
#include <string>

#if defined(_WIN32) && !defined(__MINGW32__) && !defined(__MINGW64__)
using pid_t = int;
#endif

namespace launcher {

// "steam://" / "lut://rungame/" / "heroic://launch/" URI for a bound tag.
// A tag with no kind and no target is treated as a Steam appid.
std::string uri(const Tag& tag);

// Launch a bound tag detached: Steam via the Steam client, every other kind
// via xdg-open on its URL scheme (lutris, heroic, emu). Never blocks.
void launch(const Tag& tag);

// Run a shell command fully detached via /bin/sh -c (double fork + setsid,
// stdio to /dev/null). Returns the session/process-group leader pid of the
// detached command (pass it to stop_child), or -1 if it could not be started.
pid_t run_detached(const std::string& command);

// Terminate a process started by run_detached: SIGTERM to the whole process
// group, escalate to SIGKILL after ~5 s. No-op for pid <= 0.
void stop_child(pid_t pid);

bool is_steam_kind(const std::string& kind);
bool is_emu_kind(const std::string& kind);

std::string target_id(const Tag& tag);

}  // namespace launcher