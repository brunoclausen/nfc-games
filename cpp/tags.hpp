#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct Tag {
  std::string uid;
  std::string name;
  std::uint32_t appid = 0;
  std::string kind;
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
