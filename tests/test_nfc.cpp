#include "i18n.hpp"
#include "steam.hpp"
#include "tags.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <unistd.h>

namespace fs = std::filesystem;

int failed = 0;

void check(bool ok, const char* msg) {
  if (ok) {
    std::cout << "ok  " << msg << "\n";
  } else {
    std::cerr << "FAIL  " << msg << "\n";
    ++failed;
  }
}

int main() {
  SteamGame sh;
  sh.appid = 3640596865u;
  sh.kind = "shortcut";
  check(SteamLibrary::steam_uri(sh) == "steam://rungameid/15636244473128681472",
        "Silent Hill 4 shortcut GameID");

  SteamGame agony;
  agony.appid = 2355955280u;
  agony.kind = "shortcut";
  check(SteamLibrary::steam_uri(agony) == "steam://rungameid/10118750878472077312",
        "Agony UNRATED shortcut GameID");

  SteamGame native;
  native.appid = 1367590;
  native.kind = "steam";
  check(SteamLibrary::steam_uri(native) == "steam://rungameid/1367590", "Steam appid URI");

  SteamGame rom;
  rom.exe = "\"/home/bruno/Emulation/tools/launchers/pcsx2-qt.sh\"";
  rom.launch_options = "-batch -fullscreen \"/home/bruno/Emulation/roms/ps2/Silent Hill 4 The Room.chd\"";
  const auto needle = SteamLibrary::process_needle(rom);
  check(needle.find("Silent Hill 4 The Room.chd") != std::string::npos, "ROM needle from LaunchOptions");

  SteamGame exe_only;
  exe_only.exe = "\"/home/bruno/Games/gog/agony-unrated/start.sh\"";
  check(SteamLibrary::process_needle(exe_only) == "/home/bruno/Games/gog/agony-unrated/start.sh",
        "unquoted exe needle");

  const auto tmp = fs::temp_directory_path() / ("nfc-test-" + std::to_string(::getpid()));
  fs::create_directories(tmp);
  const auto tags = tmp / "tags.conf";
  {
    auto store = TagStore::load(tags);
    store.upsert(Tag{"1D2B7022960000", "Silent Hill 4: The Room", 3640596865u, "shortcut"});
  }
  {
    auto store = TagStore::load(tags);
    auto t = store.find_uid("1d2b7022960000");
    check(t.has_value() && t->appid == 3640596865u, "tags.conf roundtrip uid");
    check(store.find("Silent Hill 4") == std::nullopt, "find requires full name");
    check(store.find("Silent Hill 4: The Room").has_value(), "find by name");
    check(store.remove("3640596865"), "remove by appid");
    check(!store.find_uid("1D2B7022960000"), "removed");
  }

  ::setenv("XDG_CONFIG_HOME", tmp.c_str(), 1);
  ::unsetenv("NFC_LANG");
  i18n_init();
  check(std::string(t("wait_tag")).find("læg") != std::string::npos, "danish wait_tag");
  check(set_lang("en"), "set_lang en");
  check(std::string(t("wait_tag")).find("place") != std::string::npos, "english wait_tag");
  check(set_lang("da"), "set_lang da");

  std::error_code ec;
  fs::remove_all(tmp, ec);

  if (failed) {
    std::cerr << failed << " tests failed\n";
    return 1;
  }
  std::cout << "all tests passed\n";
  return 0;
}
