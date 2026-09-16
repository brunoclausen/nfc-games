#pragma once

#include "tags.hpp"

#include <string>

namespace launcher {

// "steam://" / "lut://rungame/" / "heroic://launch/" URI for a bound tag.
// A tag with no kind and no target is treated as a Steam appid.
std::string uri(const Tag& tag);

// Launch a bound tag detached: Steam via the Steam client, every other kind
// via xdg-open on its URL scheme (lutris, heroic). Never blocks.
void launch(const Tag& tag);

bool is_steam_kind(const std::string& kind);

std::string target_id(const Tag& tag);

}  // namespace launcher