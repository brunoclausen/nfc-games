#include "tags.hpp"
#include "i18n.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

std::string trim(std::string_view in) {
  auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
  while (!in.empty() && is_space(static_cast<unsigned char>(in.front()))) in.remove_prefix(1);
  while (!in.empty() && is_space(static_cast<unsigned char>(in.back()))) in.remove_suffix(1);
  return std::string{in};
}

std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

bool is_kind(const std::string& s) { return s == "steam" || s == "shortcut"; }

}  // namespace

std::string normalize_uid(std::string_view raw) {
  std::string out;
  out.reserve(raw.size());
  for (unsigned char c : raw) {
    if (std::isxdigit(c)) out.push_back(static_cast<char>(std::toupper(c)));
  }
  return out;
}

std::string normalize_name(std::string_view raw) {
  std::string s = trim(raw);
  if (s.empty()) throw std::runtime_error(t("empty_name"));
  return s;
}

void migrate_legacy_tags(const std::filesystem::path& dest) {
  std::error_code ec;
  if (std::filesystem::exists(dest, ec)) return;
  std::vector<std::filesystem::path> cands = {std::filesystem::current_path() / "tags.conf"};
  if (const char* home = std::getenv("HOME"); home && *home) {
    cands.push_back(std::filesystem::path(home) / "nfc-games" / "tags.conf");
  }
  for (const auto& src : cands) {
    if (src == dest) continue;
    if (!std::filesystem::is_regular_file(src, ec)) continue;
    std::filesystem::create_directories(dest.parent_path(), ec);
    std::filesystem::copy_file(src, dest, ec);
    if (!ec) return;
  }
}

std::filesystem::path TagStore::default_path() {
  if (const char* env = std::getenv("NFC_TAGS"); env && *env) return env;
  auto dest = nfc_config_dir() / "tags.conf";
  migrate_legacy_tags(dest);
  return dest;
}

TagStore TagStore::load(const std::filesystem::path& path) {
  TagStore store;
  store.path_ = path;
  std::ifstream in(path);
  if (!in) return store;
  std::string line;
  while (std::getline(in, line)) {
    auto hash = line.find('#');
    if (hash != std::string::npos) line = line.substr(0, hash);
    line = trim(line);
    if (line.empty()) continue;
    std::istringstream iss(line);
    std::string uid, second;
    iss >> uid >> second;
    Tag t;
    t.uid = normalize_uid(uid);
    if (is_kind(second)) {
      t.kind = second;
      iss >> t.appid;
      std::string name;
      std::getline(iss, name);
      t.name = normalize_name(trim(name));
    } else {
      std::string rest;
      std::getline(iss, rest);
      t.name = normalize_name(trim(second + rest));
    }
    store.tags_.push_back(std::move(t));
  }
  return store;
}

std::optional<Tag> TagStore::find_uid(const std::string& uid) const {
  const std::string key = normalize_uid(uid);
  for (const auto& t : tags_) {
    if (t.uid == key) return t;
  }
  return std::nullopt;
}

std::optional<Tag> TagStore::find_name(const std::string& name) const {
  const std::string key = lower(normalize_name(name));
  for (const auto& t : tags_) {
    if (lower(t.name) == key) return t;
  }
  return std::nullopt;
}

std::optional<Tag> TagStore::find(const std::string& name_or_uid) const {
  if (auto t = find_name(name_or_uid)) return t;
  return find_uid(name_or_uid);
}

void TagStore::upsert(Tag tag) {
  tag.uid = normalize_uid(tag.uid);
  tag.name = normalize_name(tag.name);
  if (tag.uid.size() < 8) throw std::runtime_error(t("uid_short"));
  tags_.erase(std::remove_if(tags_.begin(), tags_.end(),
                             [&](const Tag& t) {
                               return t.uid == tag.uid || lower(t.name) == lower(tag.name) ||
                                      (tag.appid != 0 && t.appid == tag.appid);
                             }),
              tags_.end());
  tags_.push_back(std::move(tag));
  save();
}

bool TagStore::remove(const std::string& name_or_uid) {
  auto before = tags_.size();
  const std::string key_name = lower(trim(name_or_uid));
  const std::string key_uid = normalize_uid(name_or_uid);
  tags_.erase(std::remove_if(tags_.begin(), tags_.end(),
                             [&](const Tag& t) {
                               return lower(t.name) == key_name || t.uid == key_uid ||
                                      std::to_string(t.appid) == key_name;
                             }),
              tags_.end());
  if (tags_.size() == before) return false;
  save();
  return true;
}

void TagStore::save() const {
  auto parent = path_.parent_path();
  if (!parent.empty()) std::filesystem::create_directories(parent);
  auto tmp = path_;
  tmp += ".tmp";
  std::ofstream out(tmp, std::ios::trunc);
  if (!out) throw std::runtime_error(t_join("cannot_write", path_.string()));
  out << "# uid  kind  appid  name\n";
  for (const auto& tag : tags_) {
    out << tag.uid << "  " << (tag.kind.empty() ? "steam" : tag.kind) << "  " << tag.appid
        << "  " << tag.name << "\n";
  }
  out.close();
  if (!out) throw std::runtime_error(t_join("cannot_write", path_.string()));
  std::filesystem::rename(tmp, path_);
}
