#include "watch.hpp"
#include "app.hpp"
#include "i18n.hpp"
#include "steam.hpp"
#include "tags.hpp"

#include <csignal>
#include <fstream>
#include <iostream>
#include <optional>
#include <thread>
#include <unistd.h>

using namespace std::chrono_literals;

namespace {

volatile std::sig_atomic_t g_watch_run = 1;

void watch_signal(int) { g_watch_run = 0; }

}  // namespace

void nfc_watch_arm() { g_watch_run = 1; }

int cmd_watch() {
  auto old_int = std::signal(SIGINT, watch_signal);
  auto old_term = std::signal(SIGTERM, watch_signal);
  struct SignalRestore {
    void (*i)(int);
    void (*t)(int);
    ~SignalRestore() {
      std::signal(SIGINT, i == SIG_ERR ? SIG_DFL : i);
      std::signal(SIGTERM, t == SIG_ERR ? SIG_DFL : t);
    }
  } restore{old_int, old_term};

  std::error_code ec;
  std::filesystem::create_directories(nfc_config_dir(), ec);
  {
    std::ifstream in(watch_pid_path());
    int old = 0;
    if (in >> old && old > 0 && old != ::getpid() && ::kill(old, 0) == 0) {
      std::cerr << t("watch_already") << old << ")\n";
      return 0;
    }
  }
  {
    std::ofstream pidf(watch_pid_path());
    pidf << ::getpid() << "\n";
  }
  struct PidOwner {
    bool mine = true;
    ~PidOwner() {
      if (!mine) return;
      std::ifstream in(watch_pid_path());
      int p = 0;
      if (in >> p && p == ::getpid()) {
        std::error_code ec;
        std::filesystem::remove(watch_pid_path(), ec);
      }
    }
  } pid_owner;

  std::optional<Acr122> reader;
  reader = Acr122::open();
  set_led_safe(*reader, Acr122::Led::Green);
  std::cout << "watch  firmware " << reader->firmware() << "\n" << std::flush;

  std::string active_uid;
  std::uint32_t active_appid = 0;
  int present = 0;
  int absent = 0;
  int errors = 0;
  bool deaf = true;
  using clock = std::chrono::steady_clock;
  auto next_hold_picc = clock::now();

  while (g_watch_run) {
    if (usb_pause_requested()) {
      if (reader) {
        led_not_listening(*reader);
        reader.reset();
        deaf = true;
        present = 0;
        std::cout << t("watch_paused") << "\n" << std::flush;
      }
      std::this_thread::sleep_for(200ms);
      continue;
    }
    if (!reader) {
      try {
        reader = Acr122::open();
        deaf = true;
        errors = 0;
      } catch (const Acr122Error&) {
        std::this_thread::sleep_for(250ms);
        continue;
      }
    }

    std::optional<std::vector<std::uint8_t>> uid;
    bool poll_ok = true;
    const bool holding = !active_uid.empty();
    if (holding && clock::now() < next_hold_picc) {
      std::this_thread::sleep_for(150ms);
      continue;
    }
    try {
      uid = reader->try_uid(!holding);
    } catch (const Acr122Error&) {
      uid.reset();
      poll_ok = false;
    }

    if (!poll_ok) {
      present = 0;
      ++errors;
      try {
        reader->recover();
      } catch (const Acr122Error&) {
      }
      if (holding) {
        set_led_safe(*reader, Acr122::Led::Yellow);
        if (errors >= 8) reader.reset();
      } else if (errors >= 2) {
        if (!deaf) {
          deaf = true;
          std::cout << t("watch_not_listening") << "\n" << std::flush;
          led_not_listening(*reader);
        }
        reader.reset();
      } else {
        set_led_safe(*reader, Acr122::Led::Green);
      }
      std::this_thread::sleep_for(250ms);
      continue;
    }
    if (deaf) {
      deaf = false;
      errors = 0;
      if (active_uid.empty() && !uid) {
        set_led_safe(*reader, Acr122::Led::Green);
        std::cout << t("watch_ready") << "\n" << std::flush;
      } else {
        set_led_safe(*reader, Acr122::Led::Yellow);
        std::cout << t("watch_listening_again") << "\n" << std::flush;
      }
    }
    errors = 0;

    if (uid) {
      absent = 0;
      ++present;
      const std::string hex = Acr122::uid_hex(*uid);
      if (present >= 2 && hex != active_uid) {
        if (active_appid != 0) {
          std::cout << t("watch_switch_stop") << active_appid << "\n" << std::flush;
          SteamLibrary::stop(active_appid);
          active_appid = 0;
        }
        auto store = TagStore::load(TagStore::default_path());
        auto known = store.find_uid(hex);
        active_uid = hex;
        if (!known || known->appid == 0) {
          std::cout << t("watch_unknown_tag") << hex << "\n" << std::flush;
          set_led_safe(*reader, Acr122::Led::Yellow);
        } else {
          std::cout << t("watch_tag_on") << hex << "  " << known->name << "\n" << std::flush;
          signal_tag_on(*reader);
          SteamGame game;
          game.appid = known->appid;
          game.name = known->name;
          game.kind = known->kind;
          if (auto full = SteamLibrary::cached_scan().find(std::to_string(known->appid))) {
            game = *full;
          }
          for (const auto& r : SteamLibrary::running()) {
            if (r.appid != game.appid) SteamLibrary::stop(r.appid);
          }
          if (!SteamLibrary::is_running(game.appid)) {
            std::cout << t("watch_start_steam") << game.name << "  "
                      << SteamLibrary::steam_uri(game) << "\n"
                      << std::flush;
            SteamLibrary::launch(game);
          }
          active_appid = game.appid;
          set_led_safe(*reader, Acr122::Led::Yellow);
          next_hold_picc = clock::now() + 2s;
        }
      } else if (hex == active_uid) {
        set_led_safe(*reader, Acr122::Led::Yellow);
        next_hold_picc = clock::now() + 2s;
      }
    } else {
      present = 0;
      if (!active_uid.empty()) {
        try {
          reader->recover();
          uid = reader->try_uid(true);
        } catch (const Acr122Error&) {
          uid.reset();
        }
        if (uid && Acr122::uid_hex(*uid) == active_uid) {
          absent = 0;
          set_led_safe(*reader, Acr122::Led::Yellow);
          next_hold_picc = clock::now() + 2s;
          std::this_thread::sleep_for(250ms);
          continue;
        }
        ++absent;
        set_led_safe(*reader, Acr122::Led::Yellow);
        next_hold_picc = clock::now() + 400ms;
      } else {
        ++absent;
      }
      const int gone = active_uid.empty() ? 3 : 6;
      if (absent >= gone && !active_uid.empty()) {
        std::cout << t("watch_tag_off") << "\n" << std::flush;
        if (active_appid != 0) {
          std::cout << t("watch_stopping") << active_appid << "\n" << std::flush;
          SteamLibrary::stop(active_appid);
        }
        active_uid.clear();
        active_appid = 0;
        signal_tag_off(*reader);
        std::cout << t("watch_ready") << "\n" << std::flush;
      } else if (active_uid.empty() && absent > 0 && absent % 20 == 0) {
        try {
          reader->recover();
        } catch (const Acr122Error&) {
        }
      }
    }
    listen_led(*reader, !active_uid.empty());
    std::this_thread::sleep_for(250ms);
  }

  if (active_appid != 0) {
    std::cout << t("watch_stopping") << active_appid << "\n" << std::flush;
    SteamLibrary::stop(active_appid);
  }
  if (reader) led_not_listening(*reader);
  std::cout << t("watch_stopped") << "\n";
  return 0;
}
