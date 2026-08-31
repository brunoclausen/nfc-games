#pragma once

#include "acr122.hpp"
#include "steam.hpp"
#include "tags.hpp"

#include <chrono>
#include <filesystem>
#include <ostream>
#include <string>
#include <vector>

void print_help(std::ostream& out);
void usage();
std::string join_args(int argc, char** argv, int from);
std::chrono::milliseconds wait_timeout();

std::filesystem::path help_file();
std::filesystem::path udev_file();
std::filesystem::path udev_helper();
int run_udev_install(const std::filesystem::path& helper, const std::filesystem::path& rule);

std::filesystem::path watch_pid_path();
bool usb_pause_requested();

struct UsbPause {
  UsbPause();
  ~UsbPause();
  UsbPause(const UsbPause&) = delete;
  UsbPause& operator=(const UsbPause&) = delete;
};

Acr122 open_reader();
Acr122::Led parse_led(const std::string& name);
void set_led_safe(Acr122& r, Acr122::Led led);
void beep_safe(Acr122& r, std::chrono::milliseconds duration, int times = 1);
void signal_tag_on(Acr122& r);
void signal_tag_off(Acr122& r);
void listen_led(Acr122& r, bool tag_on);
void led_not_listening(Acr122& r);
void demo(Acr122& r);
void print_tag(const std::string& uid, const TagStore& store);
std::vector<uint8_t> wait_for_tag(Acr122& reader);
int resolve_game(const std::string& query, SteamGame& out);
