#include "i18n.hpp"
#include "ndef.hpp"
#include "steam.hpp"
#include "tags.hpp"
#include "watch.hpp"

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

#ifndef NFC_VERSION
#define NFC_VERSION "dev"
#endif

int main() {
  check(std::string(NFC_VERSION).find('.') != std::string::npos, "version looks like x.y.z");

  SteamGame sh;
  sh.appid = 3640596865u;
  sh.kind = "shortcut";
  check(SteamLibrary::steam_uri(sh) == "steam://rungameid/15636244473128681472",
        "non-Steam shortcut GameID");

  SteamGame agony;
  agony.appid = 2355955280u;
  agony.kind = "shortcut";
  check(SteamLibrary::steam_uri(agony) == "steam://rungameid/10118750878472077312",
        "second non-Steam shortcut GameID");

  SteamGame native;
  native.appid = 1367590;
  native.kind = "steam";
  check(SteamLibrary::steam_uri(native) == "steam://rungameid/1367590", "Steam appid URI");

  SteamGame rom;
  rom.exe = "\"/tmp/emu/launchers/pcsx2-qt.sh\"";
  rom.launch_options = "-batch -fullscreen \"/tmp/emu/roms/ps2/Example Game.chd\"";
  const auto needle = SteamLibrary::process_needle(rom);
  check(needle.find("Example Game.chd") != std::string::npos, "ROM needle from LaunchOptions");

  SteamGame exe_only;
  exe_only.exe = "\"/tmp/games/gog/example/start.sh\"";
  check(SteamLibrary::process_needle(exe_only) == "/tmp/games/gog/example/start.sh",
        "unquoted exe needle");

  const auto tmp = fs::temp_directory_path() / ("nfc-test-" + std::to_string(::getpid()));
  fs::create_directories(tmp);
  const auto tags = tmp / "tags.conf";
  {
    auto store = TagStore::load(tags);
    store.upsert(Tag{"04AABBCCDD0000", "Example Game", 3640596865u, "shortcut"});
  }
  {
    auto store = TagStore::load(tags);
    auto t = store.find_uid("04aabbccdd0000");
    check(t.has_value() && t->appid == 3640596865u, "tags.conf roundtrip uid");
    check(store.find("Example") == std::nullopt, "find requires full name");
    check(store.find("Example Game").has_value(), "find by name");
    check(store.remove("3640596865"), "remove by appid");
    check(!store.find_uid("04AABBCCDD0000"), "removed");
  }

  {
    TagTracker tr;
    check(tr.feed("AA") == TagTracker::Event::None, "first AA is candidate");
    check(tr.feed("AA") == TagTracker::Event::On, "second AA turns on");
    check(tr.feed("BB") == TagTracker::Event::None, "first BB is switch candidate");
    check(tr.feed("") == TagTracker::Event::None, "empty during swap keeps candidate");
    check(tr.feed("BB") == TagTracker::Event::Switch, "second BB switches");
    check(tr.active == "BB", "active is BB after switch");
    for (int i = 0; i < TagTracker::kGone - 1; ++i) {
      check(tr.feed("") == TagTracker::Event::None, "absent not gone yet");
    }
    check(tr.feed("") == TagTracker::Event::Off, "kGone empties is off");
    check(tr.active.empty(), "active cleared on off");
  }

  {
    std::uint8_t blank[4] = {0, 0, 0, 0};
    check(!parse_type2_cc(blank).valid, "blank CC is invalid");
    std::uint8_t ntag213[4] = {0xE1, 0x10, 0x12, 0x00};
    auto cc = parse_type2_cc(ntag213);
    check(cc.valid && cc.writable && cc.data_size == 144, "NTAG213 CC");
    std::uint8_t ro[4] = {0xE1, 0x10, 0x12, 0x0F};
    check(parse_type2_cc(ro).valid && !parse_type2_cc(ro).writable, "read-only CC");
    auto made = make_type2_cc(144);
    check(made[0] == 0xE1 && made[2] == 0x12 && made[3] == 0x00, "make NTAG213 CC");
    std::vector<std::uint8_t> ver213{0x00, 0x04, 0x04, 0x02, 0x01, 0x00, 0x0F, 0x03};
    check(ntag_user_bytes_from_version(ver213) == 144, "GET_VERSION NTAG213");
    std::vector<std::uint8_t> ver216{0x00, 0x04, 0x04, 0x02, 0x01, 0x00, 0x13, 0x03};
    check(ntag_user_bytes_from_version(ver216) == 888, "GET_VERSION NTAG216");

    auto empty_tlv = std::vector<std::uint8_t>{0x03, 0x00, 0xFE};
    check(!decode_type2_ndef_text(empty_tlv), "empty NDEF is null");
    auto factory_ul = std::vector<std::uint8_t>{0x01, 0x03, 0xA0, 0x0C, 0x34, 0x03, 0x00, 0xFE};
    check(!decode_type2_ndef_text(factory_ul), "factory Ultralight empty NDEF is null");
    auto img = encode_type2_ndef_text("Silent Hill 4: The Room", "da", 144);
    check(!img.empty() && img[0] == 0x03 && img.back() != 0x03, "encode Type 2 image");
    auto got = decode_type2_ndef_text(img);
    check(got && *got == "Silent Hill 4: The Room", "NDEF text roundtrip");
    auto long_name = encode_type2_ndef_text(
        "Resident Evil - Code - Veronica X (USA) (Disc 1).nkit", "da", 144);
    auto long_got = decode_type2_ndef_text(long_name);
    check(long_got && long_got->find("Resident Evil") == 0, "long game name fits NTAG213");
    auto tiny = encode_type2_ndef_text("Hello", "en", 8);
    check(tiny.empty(), "too-small tag yields empty encode");
  }

  check(normalize_uid("04aa:bb-cc dd") == "04AABBCCDD", "normalize_uid strips junk");
  check(normalize_uid("1d2b7022960000") == "1D2B7022960000", "normalize_uid upper");
  {
    auto empty = TagStore::load(tmp / "missing.conf");
    check(empty.all().empty(), "missing tags.conf is empty");
    check(!empty.find_uid("00"), "empty store has no uid");
  }

  ::setenv("XDG_CONFIG_HOME", tmp.c_str(), 1);
  ::setenv("NFC_LANG", "da", 1);
  i18n_init();
  check(std::string(t("wait_tag")).find("læg") != std::string::npos, "danish wait_tag");
  check(std::string(t("ndef_empty")).find("tom") != std::string::npos, "danish ndef_empty");
  check(set_lang("en"), "set_lang en");
  check(std::string(t("wait_tag")).find("place") != std::string::npos, "english wait_tag");
  check(std::string(t("watch_already")).find("already") != std::string::npos, "english watch_already");
  check(std::string(t("restart_ok")).find("restarted") != std::string::npos, "english restart_ok");
  check(std::string(t("watch_started")).find("started") != std::string::npos, "english watch_started");
  check(std::string(t("watch_usb_reset")).find("USB") != std::string::npos, "english watch_usb_reset");
  check(set_lang("da"), "set_lang da");
  check(std::string(t("watch_already")).find("kører") != std::string::npos, "danish watch_already");
  check(std::string(t("restart_ok")).find("genstartet") != std::string::npos, "danish restart_ok");
  check(std::string(t("menu_text")).find("watch") != std::string::npos, "danish menu_text");
  check(set_lang("en"), "set_lang en again");
  check(std::string(t("menu_text")).find("ready") != std::string::npos, "english menu_text");
  check(set_lang("de"), "set_lang de");
  check(std::string(t("wait_tag")).find("Tag") != std::string::npos, "german wait_tag");
  check(set_lang("sv"), "set_lang sv");
  check(std::string(t("wait_tag")).find("tagg") != std::string::npos, "swedish wait_tag");
  check(set_lang("nb"), "set_lang nb");
  check(std::string(t("wait_tag")).find("brikke") != std::string::npos, "norwegian wait_tag");
  check(set_lang("nn"), "set_lang nn alias");
  check(current_lang_code() == "nb", "nn maps to nb");
  check(set_lang("no_NO.UTF-8"), "set_lang no_NO.UTF-8");
  check(current_lang_code() == "nb", "no_NO maps to nb");
  check(set_lang("fr"), "set_lang fr");
  check(std::string(t("wait_tag")).find("tag") != std::string::npos, "french wait_tag");
  check(languages().size() >= 6, "six languages listed");
  check(set_lang("da"), "set_lang da again");
  check(!set_lang("xx"), "unknown language rejected");

  std::error_code ec;
  fs::remove_all(tmp, ec);

  if (failed) {
    std::cerr << failed << " tests failed\n";
    return 1;
  }
  std::cout << "all tests passed\n";
  return 0;
}
