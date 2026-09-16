#include "watch.hpp"
#include "app.hpp"
#include "i18n.hpp"
#include "launch.hpp"
#include "steam.hpp"
#include "tags.hpp"

#include <chrono>
#include <csignal>
#include <fstream>
#include <iostream>
#include <optional>
#include <thread>
#include <unistd.h>

using namespace std::chrono_literals;

namespace {

volatile std::sig_atomic_t g_watch_run = 1;

// ACR122U firmware hangs after hours of PICC polling. Cycle the RF field
// (no USB reset) often enough to keep it alive. USB reopen is backup;
// libusb_reset_device is last resort — it has killed the host xHCI controller.
constexpr auto kRfRefreshIdle = 2min;
constexpr auto kRfRefreshHold = 5min;
constexpr auto kUsbReopenEvery = 45min;
constexpr auto kHwResetCooldown = 10min;
constexpr auto kReopenSettle = 400ms;
// An unbound tag left on the reader retries at this interval, so registering it
// with `nfc add` takes effect without lifting it. Retrying every poll would
// re-read tags.conf ~3x/s and starve the RF keep-alive below.
constexpr auto kRebindRetry = 3s;

std::string trim_hook(std::string s) {
  auto is_sp = [](unsigned char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
  while (!s.empty() && is_sp(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
  while (!s.empty() && is_sp(static_cast<unsigned char>(s.back()))) s.pop_back();
  return s;
}

// Optional commands in ~/.config/nfc-games/nfc.conf, re-read on every fire:
//   hook_start=...   hook_stop=...
std::string hook_command(bool start) {
  const std::string want = start ? "hook_start" : "hook_stop";
  std::ifstream in(lang_config_path());
  if (!in) return {};
  std::string line;
  while (std::getline(in, line)) {
    const auto eq = line.find('=');
    if (eq == std::string::npos) continue;
    if (trim_hook(line.substr(0, eq)) != want) continue;
    return trim_hook(line.substr(eq + 1));
  }
  return {};
}

// Run a configured hook as a detached `sh -c` with $1=id, $2=game name.
// id is the Steam appid for Steam kinds and the slug/app_name otherwise.
void run_hook(bool start, const std::string& id, const std::string& name) {
  const std::string cmd = hook_command(start);
  if (cmd.empty()) return;
  std::cout << (start ? "hook start " : "hook stop ") << id << "  " << name << "\n"
            << std::flush;
  const pid_t pid = ::fork();
  if (pid != 0) return;
  ::setsid();
  ::execl("/bin/sh", "sh", "-c", cmd.c_str(), "nfc", id.c_str(), name.c_str(),
          static_cast<char*>(nullptr));
  ::_exit(127);
}

// Stop the active game if it is a Steam game (only Steam kinds can be stopped
// automatically). Non-Steam kinds are left running; the user closes them.
void stop_active(const Tag& active) {
  if (launcher::is_steam_kind(active.kind)) {
    SteamLibrary::stop(active.appid);
    run_hook(false, std::to_string(active.appid), active.name);
  } else {
    std::cout << t("watch_nonsteam_keep") << "  " << active.name << "\n" << std::flush;
  }
}

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
  TagTracker tracker;
  std::optional<Tag> active;
  int errors = 0;
  int drop_streak = 0;
  int hold_polls = 0;
  bool deaf = true;
  bool waiting_reader = false;
  std::optional<Acr122::Led> shown_led;
  using clock = std::chrono::steady_clock;
  auto last_usb_open = clock::now();
  auto last_rf_refresh = clock::now();
  auto last_hw_reset = clock::time_point{};
  auto last_rebind = clock::time_point{};
  auto idle_begin = clock::now();
  std::string unknown_logged;

  auto show_led = [&](Acr122::Led led) {
    if (!reader) return;
    if (shown_led && *shown_led == led) return;
    set_led_safe(*reader, led);
    shown_led = led;
  };

  auto drop_reader = [&](bool mark_deaf) {
    if (!reader) return;
    if (mark_deaf) {
      try {
        led_not_listening(*reader);
      } catch (const Acr122Error&) {
      }
      deaf = true;
    }
    reader.reset();
    shown_led.reset();
    errors = 0;
    last_usb_open = clock::now();
    std::this_thread::sleep_for(kReopenSettle);
  };

  auto maybe_hw_reset_and_drop = [&](bool mark_deaf) {
    const auto now = clock::now();
    ++drop_streak;
    if (drop_streak >= 3 &&
        (last_hw_reset == clock::time_point{} || now - last_hw_reset >= kHwResetCooldown) &&
        reader) {
      try {
        reader->reset_hw();
      } catch (const Acr122Error&) {
      }
      last_hw_reset = now;
      std::cout << t("watch_usb_reset") << "\n" << std::flush;
    } else {
      std::cout << t("watch_usb_reopen") << "\n" << std::flush;
    }
    drop_reader(mark_deaf);
  };

  auto rf_refresh = [&] {
    if (!reader) return false;
    try {
      reader->refresh();
      last_rf_refresh = clock::now();
      shown_led.reset();
      return true;
    } catch (const Acr122Error&) {
      drop_reader(false);
      return false;
    }
  };

  // A daemon must not die on a bad tags.conf or a failed fork, so everything
  // that touches the filesystem or Steam is contained here.
  auto bind_one = [&]() {
    const std::string& hex = tracker.active;
    auto store = TagStore::load(TagStore::default_path());
    auto known = store.find_uid(hex);
    if (!known || (known->appid == 0 && known->target.empty())) {
      // Warn once per tag, not once per retry.
      if (unknown_logged != hex) {
        std::cout << t("watch_unknown_tag") << hex << "\n" << std::flush;
        unknown_logged = hex;
      }
      show_led(Acr122::Led::Yellow);
      active.reset();
      return;
    }
    unknown_logged.clear();
    std::cout << t("watch_tag_on") << hex << "  " << known->name << "\n" << std::flush;
    signal_tag_on(*reader);
    shown_led = Acr122::Led::Yellow;
    if (launcher::is_steam_kind(known->kind)) {
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
        run_hook(true, std::to_string(game.appid), game.name);
      }
    } else {
      // lutris/heroic: launch only. The game is not stopped when the tag is
      // lifted (there is no generic stop for these kinds).
      std::cout << t("watch_start_other") << known->name << "  " << launcher::uri(*known)
                << "\n"
                << std::flush;
      launcher::launch(*known);
      run_hook(true, known->target, known->name);
    }
    active = *known;
    show_led(Acr122::Led::Yellow);
  };

  auto bind_active = [&]() {
    try {
      bind_one();
    } catch (const std::exception& e) {
      std::cerr << "watch: " << e.what() << "\n" << std::flush;
    }
  };

  while (g_watch_run) {
    if (usb_pause_requested()) {
      if (reader) {
        drop_reader(true);
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
        waiting_reader = false;
        shown_led.reset();
        last_usb_open = clock::now();
        last_rf_refresh = clock::now();
        std::cout << "watch  firmware " << reader->firmware() << "\n" << std::flush;
      } catch (const Acr122Error&) {
        if (!waiting_reader) {
          waiting_reader = true;
          std::cout << t("watch_waiting_reader") << "\n" << std::flush;
        }
        std::this_thread::sleep_for(1s);
        continue;
      }
    }

    std::optional<std::vector<std::uint8_t>> uid;
    bool poll_ok = true;
    const bool holding = !tracker.active.empty();
    try {
      uid = reader->try_uid(!holding);
      if (holding) {
        ++hold_polls;
        // InList every ~3s so a swap A→B is seen without waiting for tag-off.
        const bool stale_check = (hold_polls % 8 == 0);
        if (!uid || stale_check) {
          auto u2 = reader->try_uid(true);
          if (u2) uid = std::move(u2);
        }
      }
    } catch (const Acr122Error&) {
      uid.reset();
      poll_ok = false;
    }

    if (!poll_ok) {
      ++errors;
      if (!rf_refresh() && !reader) {
        std::this_thread::sleep_for(250ms);
        continue;
      }
      if (holding) {
        show_led(Acr122::Led::Yellow);
        if (errors >= 6) maybe_hw_reset_and_drop(false);
      } else if (errors >= 2) {
        if (!deaf) {
          deaf = true;
          std::cout << t("watch_not_listening") << "\n" << std::flush;
          show_led(Acr122::Led::Red);
        }
        if (errors >= 4) maybe_hw_reset_and_drop(false);
      } else {
        show_led(Acr122::Led::Green);
      }
      std::this_thread::sleep_for(250ms);
      continue;
    }
    if (deaf) {
      deaf = false;
      errors = 0;
      if (tracker.active.empty() && !uid) {
        show_led(Acr122::Led::Green);
        std::cout << t("watch_ready") << "\n" << std::flush;
      } else {
        show_led(Acr122::Led::Yellow);
        std::cout << t("watch_listening_again") << "\n" << std::flush;
      }
    }
    errors = 0;
    drop_streak = 0;
    if (!holding) hold_polls = 0;

    const std::string hex = uid ? Acr122::uid_hex(*uid) : std::string{};
    const auto ev = tracker.feed(hex);
    if (ev == TagTracker::Event::On || ev == TagTracker::Event::Switch) {
      if (ev == TagTracker::Event::Switch && active) {
        std::cout << t("watch_switch_stop") << active->name << "\n" << std::flush;
        stop_active(*active);
        active.reset();
      }
      if (ev == TagTracker::Event::Switch) {
        rf_refresh();
        if (!reader) {
          std::this_thread::sleep_for(250ms);
          continue;
        }
      }
      idle_begin = clock::now();
      last_rebind = clock::now();
      bind_active();
    } else if (ev == TagTracker::Event::None && !tracker.active.empty() &&
               !active && !hex.empty() && hex == tracker.active &&
               clock::now() - last_rebind >= kRebindRetry) {
      last_rebind = clock::now();
      bind_active();
    } else if (ev == TagTracker::Event::Off) {
      std::cout << t("watch_tag_off") << "\n" << std::flush;
      if (active) {
        std::cout << t("watch_stopping") << active->name << "\n" << std::flush;
        stop_active(*active);
      }
      active.reset();
      hold_polls = 0;
      idle_begin = clock::now();
      unknown_logged.clear();
      signal_tag_off(*reader);
      shown_led = Acr122::Led::Green;
      rf_refresh();
      show_led(Acr122::Led::Green);
    } else if (clock::now() - last_usb_open >= kUsbReopenEvery) {
      std::cout << t("watch_usb_reopen") << "\n" << std::flush;
      drop_reader(false);
    } else if (clock::now() - last_rf_refresh >=
               (tracker.active.empty() ? kRfRefreshIdle : kRfRefreshHold)) {
      rf_refresh();
      if (reader) {
        show_led(tracker.active.empty() ? Acr122::Led::Green : Acr122::Led::Yellow);
      }
    } else if (!tracker.active.empty()) {
      idle_begin = clock::now();
    }
    if (reader) {
      show_led(tracker.active.empty() ? Acr122::Led::Green : Acr122::Led::Yellow);
    }
    const auto idle_for = clock::now() - idle_begin;
    std::this_thread::sleep_for(idle_for > 30s ? 1000ms : (holding ? 400ms : 250ms));
  }

  if (active) {
    std::cout << t("watch_stopping") << active->name << "\n" << std::flush;
    stop_active(*active);
  }
  if (reader) led_not_listening(*reader);
  std::cout << t("watch_stopped") << "\n";
  return 0;
}
