#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

struct Language {
  const char* code;
  const char* name;
};

void i18n_init();
std::vector<Language> languages();
std::string current_lang_code();
const char* current_lang_name();
std::filesystem::path lang_config_path();
bool set_lang(std::string_view code);
const char* t(const char* key);
std::string t_join(const char* key, const std::string& extra);
