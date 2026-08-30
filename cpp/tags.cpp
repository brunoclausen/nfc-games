#include "tags.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>
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
  if (s.empty()) throw std::runtime_error("tomt navn");
  return s;
}

std::filesystem::path TagStore::default_path() {
  if (const char* env = std::getenv("NFC_TAGS")) return env;
  return std::filesystem::current_path() / "tags.conf";
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
    std::string uid, name;
    iss >> uid;
    std::getline(iss, name);
    name = trim(name);
    if (name.empty()) name = uid;
    store.tags_.push_back({normalize_uid(uid), normalize_name(name)});
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

void TagStore::upsert(const std::string& uid, const std::string& name) {
  const std::string u = normalize_uid(uid);
  const std::string n = normalize_name(name);
  if (u.size() < 8) throw std::runtime_error("UID for kort");
  tags_.erase(std::remove_if(tags_.begin(), tags_.end(),
                             [&](const Tag& t) {
                               return t.uid == u || lower(t.name) == lower(n);
                             }),
              tags_.end());
  tags_.push_back({u, n});
  save();
}

bool TagStore::remove(const std::string& name_or_uid) {
  auto before = tags_.size();
  const std::string key_name = lower(trim(name_or_uid));
  const std::string key_uid = normalize_uid(name_or_uid);
  tags_.erase(std::remove_if(tags_.begin(), tags_.end(),
                             [&](const Tag& t) {
                               return lower(t.name) == key_name || t.uid == key_uid;
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
  if (!out) throw std::runtime_error("kan ikke skrive " + path_.string());
  out << "# uid  navn\n";
  for (const auto& t : tags_) {
    out << t.uid << "  " << t.name << "\n";
  }
  out.close();
  if (!out) throw std::runtime_error("kan ikke skrive " + path_.string());
  std::filesystem::rename(tmp, path_);
}
