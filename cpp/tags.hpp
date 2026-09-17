#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

struct Tag {
  std::string uid;
  std::string name;
  std::uint32_t appid = 0;  // steam/shortcut only
  std::string kind;         // "steam" | "shortcut" | "lutris" | "heroic" | "action" | "emu"
  std::string target;       // lutris slug / heroic app_name / shell command (action, emu); empty for Steam
  std::string target_off;   // action only: shell command run when the tag is lifted
};

class TagStore {
 public:
  static std::filesystem::path default_path();
  static TagStore load(const std::filesystem::path& path);

  const std::filesystem::path& path() const { return path_; }
  const std::vector<Tag>& all() const { return tags_; }

  std::optional<Tag> find_uid(const std::string& uid) const;
  std::optional<Tag> find_name(const std::string& name) const;
  std::optional<Tag> find(const std::string& name_or_uid) const;

  void upsert(Tag tag);
  bool remove(const std::string& name_or_uid);
  void save() const;

 private:
  std::filesystem::path path_;
  std::vector<Tag> tags_;
};

std::string normalize_uid(std::string_view raw);
std::string normalize_name(std::string_view raw);

// Split an action command "ON || OFF" into (target, target_off). Returns only
// target when there is no " || " separator.
std::pair<std::string, std::string> split_action(const std::string& raw);
