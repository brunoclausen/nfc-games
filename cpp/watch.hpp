#pragma once

#include <string>
#include <string_view>

int cmd_watch();
void nfc_watch_arm();

// Debounce for watch: two hits to accept a tag, six misses to drop it.
// Empty reads do not reset a new-tag candidate, so swapping A→B still works.
struct TagTracker {
  std::string active;
  std::string candidate;
  int hits = 0;
  int absent = 0;

  static constexpr int kConfirm = 2;
  static constexpr int kGone = 6;

  enum class Event { None, On, Switch, Off };

  Event feed(std::string_view hex) {
    if (!hex.empty()) {
      absent = 0;
      if (hex == active) {
        candidate.clear();
        hits = 0;
        return Event::None;
      }
      if (hex != candidate) {
        candidate.assign(hex.begin(), hex.end());
        hits = 1;
      } else {
        ++hits;
      }
      if (hits >= kConfirm) {
        const bool switching = !active.empty();
        active = candidate;
        candidate.clear();
        hits = 0;
        return switching ? Event::Switch : Event::On;
      }
      return Event::None;
    }
    ++absent;
    if (!active.empty() && absent >= kGone) {
      active.clear();
      candidate.clear();
      hits = 0;
      absent = 0;
      return Event::Off;
    }
    return Event::None;
  }
};
