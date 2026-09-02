#pragma once

#include <string>

int cmd_help();
int cmd_start(const std::string& query);
int cmd_lock();
int cmd_stop(const std::string& query);
int cmd_games();
int cmd_list();
int cmd_read();
int cmd_add(const std::string& query);
int cmd_write();
int cmd_remove(const std::string& key);
int cmd_udev(const std::string& arg);
int cmd_lang(const std::string& want);
int cmd_restart();
int cmd_start_watch();
