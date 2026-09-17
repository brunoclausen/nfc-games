#include "i18n.hpp"
#include "launch.hpp"
#include "ndef.hpp"
#include "roms.hpp"
#include "steam.hpp"
#include "tags.hpp"
#include "watch.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
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

  {
    // AppId= must match a whole number: 220 (Half-Life 2) must not match
    // 2200 (Quake III), or `nfc stop` kills the wrong game's process tree.
    const std::string q3 = "reaper SteamLaunch AppId=2200 -- /usr/bin/quake3";
    check(SteamLibrary::cmd_matches_appid(q3, 2200), "AppId matches itself");
    check(!SteamLibrary::cmd_matches_appid(q3, 220), "AppId=220 does not match AppId=2200");
    check(!SteamLibrary::cmd_matches_appid(q3, 22), "AppId=22 does not match AppId=2200");
    check(!SteamLibrary::cmd_matches_appid(q3, 0), "appid 0 never matches");
    const std::string hl2 = "reaper SteamLaunch AppId=220 -- /usr/bin/hl2";
    check(SteamLibrary::cmd_matches_appid(hl2, 220), "trailing AppId at end of cmdline");
    check(!SteamLibrary::cmd_matches_appid(hl2, 2200), "shorter cmdline does not match longer id");
  }

  {
    // tags.conf: '#' only comments out a whole line. Real Steam titles such as
    // "#DRIVE" used to throw and take the watch daemon down with them.
    const auto hashes = tmp / "hash.conf";
    {
      std::ofstream out(hashes);
      out << "# uid  kind  appid  name\n";
      out << "04AABBCCDD  steam  547650  #DRIVE\n";
      out << "04AABBCCDE  steam  400  Portal #2 Deluxe\n";
      out << "not-a-valid-line\n";
      out << "04AABBCCDF  steam  620  Portal 2\n";
    }
    auto store = TagStore::load(hashes);
    check(store.all().size() == 3, "malformed line skipped, rest still loads");
    auto drive = store.find_uid("04AABBCCDD");
    check(drive && drive->name == "#DRIVE", "name may start with '#'");
    auto portal = store.find_uid("04AABBCCDE");
    check(portal && portal->name == "Portal #2 Deluxe", "name keeps an inner '#'");
  }

  {
    // A corrupt shortcuts.vdf costs two bytes per nesting level, so an
    // unbounded parser recursed the stack to death. This must just return.
    const auto root = tmp / "fakesteam";
    fs::create_directories(root / "userdata" / "1" / "config");
    {
      std::ofstream out(root / "userdata" / "1" / "config" / "shortcuts.vdf",
                        std::ios::binary);
      out.put('\0');
      out << "shortcuts";
      out.put('\0');
      for (int i = 0; i < 200000; ++i) {
        out.put('\0');
        out.put('\0');
      }
    }
    ::setenv("HOME", tmp.c_str(), 1);
    ::setenv("STEAM_DIR", root.c_str(), 1);
    auto lib = SteamLibrary::scan();
    check(lib.games().empty(), "deeply nested shortcuts.vdf is rejected, not fatal");
    ::unsetenv("STEAM_DIR");
  }

  check(normalize_uid("04aa:bb-cc dd") == "04AABBCCDD", "normalize_uid strips junk");
  check(normalize_uid("1d2b7022960000") == "1D2B7022960000", "normalize_uid upper");
  {
    auto empty = TagStore::load(tmp / "missing.conf");
    check(empty.all().empty(), "missing tags.conf is empty");
    check(!empty.find_uid("00"), "empty store has no uid");
  }

  {
    // lutris/heroic kinds: target (slug/app_name) roundtrips in tags.conf.
    const auto conf = tmp / "other.conf";
    {
      auto store = TagStore::load(conf);
      store.upsert(Tag{"04B1B2B30001", "Hades 2", 0, "lutris", "hades"});
      store.upsert(Tag{"04B1B2B30002", "Control Ultimate Edition", 0, "heroic", "Control"});
    }
    {
      auto store = TagStore::load(conf);
      auto lut = store.find_uid("04b1b2b30001");
      check(lut && lut->kind == "lutris" && lut->target == "hades" && lut->appid == 0,
            "lutris slug roundtrip");
      auto her = store.find_uid("04b1b2b30002");
      check(her && her->kind == "heroic" && her->target == "Control",
            "heroic app_name roundtrip");
      check(store.find("hades").has_value(), "find by lutris target");
      check(store.remove("hades"), "remove by lutris target");
      check(!store.find_uid("04B1B2B30001"), "lutris removed");
    }
  }

  {
    // action kind: shell command roundtrip in tags.conf.
    const auto conf = tmp / "action.conf";
    {
      auto store = TagStore::load(conf);
      store.upsert(Tag{"04B1B2B30010", "~/bin/party.sh on", 0, "action", "~/bin/party.sh on"});
      store.upsert(Tag{"04B1B2B30011", "~/bin/lamp.sh on", 0, "action", "~/bin/lamp.sh on"});
    }
    {
      auto store = TagStore::load(conf);
      auto a = store.find_uid("04b1b2b30010");
      check(a && a->kind == "action" && a->target == "~/bin/party.sh on" &&
                a->name == "~/bin/party.sh on" && a->appid == 0,
            "action roundtrip");
      check(store.find("~/bin/party.sh on").has_value(), "find by action command");
      check(store.remove("~/bin/party.sh on"), "remove by action command");
      check(!store.find_uid("04B1B2B30010"), "action removed");
    }
    check(split_action("on").first == "on" && split_action("on").second.empty(),
          "split_action plain");
    check(split_action("lamp on || lamp off").first == "lamp on" &&
              split_action("lamp on || lamp off").second == "lamp off",
          "split_action on/off");
    check(split_action("lamp on || ").first == "lamp on" &&
              split_action("lamp on || ").second.empty(),
          "split_action trailing separator");
  }

  {
    // action kind with off command roundtrips with the " || " separator.
    const auto conf = tmp / "action_off.conf";
    {
      auto store = TagStore::load(conf);
      Tag t{"04B1B2B30020", "lamp on", 0, "action", "lamp on"};
      t.target_off = "lamp off";
      store.upsert(std::move(t));
    }
    {
      auto store = TagStore::load(conf);
      auto t = store.find_uid("04b1b2b30020");
      check(t && t->target == "lamp on" && t->target_off == "lamp off",
            "action off roundtrip");
    }
  }

  {
    // emu kind: the whole rest of the line after "emu" is the command.
    const auto conf = tmp / "emu.conf";
    {
      auto store = TagStore::load(conf);
      store.upsert(Tag{"04B1B2B30030", "dolphin -e ~/roms/MK.iso", 0, "emu",
                       "dolphin -e ~/roms/MK.iso"});
    }
    {
      auto store = TagStore::load(conf);
      auto e = store.find_uid("04b1b2b30030");
      check(e && e->kind == "emu" && e->target == "dolphin -e ~/roms/MK.iso" &&
                e->name == "dolphin -e ~/roms/MK.iso" && e->appid == 0,
            "emu roundtrip");
      check(store.find("dolphin -e ~/roms/MK.iso").has_value(), "find by emu command");
      check(store.remove("dolphin -e ~/roms/MK.iso"), "remove by emu command");
      check(!store.find_uid("04B1B2B30030"), "emu removed");
    }
  }

  {
    const Tag steam{"AA", "Half-Life 2", 220, "steam", ""};
    check(launcher::uri(steam) == "steam://rungameid/220", "steam tag URI");
    check(launcher::target_id(steam) == "220", "steam target id");
    const Tag sc{"AA", "Shortcut", 3640596865u, "shortcut", ""};
    check(launcher::uri(sc) == "steam://rungameid/15636244473128681472",
          "shortcut tag URI uses GameID");
    const Tag lut{"AA", "Hades", 0, "lutris", "hades"};
    check(launcher::uri(lut) == "lutris://rungame/hades", "lutris tag URI");
    check(launcher::target_id(lut) == "hades", "lutris target id");
    const Tag her{"AA", "Control", 0, "heroic", "Control"};
    check(launcher::uri(her) == "heroic://launch/Control", "heroic tag URI");
    const Tag emu{"AA", "dolphin -e mk.elf", 0, "emu", "dolphin -e mk.elf"};
    check(launcher::is_emu_kind("emu") && launcher::uri(emu) == "dolphin -e mk.elf",
          "emu tag URI is its command");
    check(launcher::is_steam_kind("lutris") == false, "lutris is not a Steam kind");
    check(launcher::is_steam_kind("action") == false, "action is not a Steam kind");
    check(launcher::is_steam_kind("emu") == false, "emu is not a Steam kind");
  }

  {
    const auto exts = glob_extensions("**/${title}@(.RVZ|.rvz|.iso|.ISO|.chd)");
    check(exts.size() == 3 && exts[0] == ".rvz" && exts[1] == ".iso" && exts[2] == ".chd",
          "glob extensions dedupe and lowercase");

    check(build_emu_command("/e/dolphin.sh", "-b -e \"${filePath}\"", "/r/My Game.rvz") ==
              "/e/dolphin.sh -b -e \"/r/My Game.rvz\"",
          "emu command fills filePath");
    check(build_emu_command("/e/pcsx2.sh", "vblank_mode=0 %command% -fullscreen \"${filePath}\"",
                            "/r/g.iso") == "/e/pcsx2.sh -fullscreen \"/r/g.iso\"",
          "emu command strips %command% and vblank");
    check(build_emu_command("/e/retroarch.sh", "-L ${racores}/snes.so \"${filePath}\"",
                            "/r/g.sfc") == "/e/retroarch.sh '/r/g.sfc'",
          "emu command falls back on unknown vars");

    const std::string js = R"json([
      {"configTitle":"Sony PlayStation 2 - PCSX2","romDirectory":"${romsdirglobal}/ps2",
       "disabled":false,"executableArgs":"-fullscreen \"${filePath}\"",
       "parserInputs":{"glob":"**/${title}@(.iso|.chd)"},
       "executable":{"path":"/e/pcsx2.sh"}}
    ])json";
    const auto parsers = parse_srm_config(js, "/roms");
    check(parsers.size() == 1 && parsers[0].rom_dir == "/roms/ps2" &&
              parsers[0].launcher == "/e/pcsx2.sh" && parsers[0].exts.size() == 2,
          "srm parser parse and romsdir expansion");

    const auto root = tmp / "emulib";
    fs::create_directories(root / "data" / "roms" / "gc");
    {
      std::ofstream(root / "data" / "roms" / "gc" / "My Game.nkit.rvz") << "x";
      std::ofstream(root / "data" / "roms" / "gc" / "notes.txt") << "x";
      std::ofstream(root / "data" / "srm.json")
          << R"json([{"configTitle":"GameCube - Dolphin","romDirectory":"${romsdirglobal}/gc",)"
             R"json("disabled":false,"executableArgs":"-b -e \"${filePath}\"",)"
             R"json("parserInputs":{"glob":"**/${title}@(.rvz|.nkit)"},)"
             R"json("executable":{"path":"/e/dolphin.sh"}}])json";
    }
    ::setenv("NFC_SRM_CONFIG", (root / "data" / "srm.json").c_str(), 1);
    ::setenv("NFC_ROMS_DIR", (root / "data" / "roms").c_str(), 1);
    const auto lib = EmuLibrary::scan();
    check(lib.games().size() == 1, "emu scan finds one rom");
    check(!lib.games().empty() && lib.games()[0].system == "gc" &&
              lib.games()[0].name == "My Game" &&
              lib.games()[0].command == "/e/dolphin.sh -b -e \"" + lib.games()[0].path + "\"",
          "emu scan strips .nkit and builds command");
    ::unsetenv("NFC_SRM_CONFIG");
    ::unsetenv("NFC_ROMS_DIR");
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
  check(std::string(t("watch_usb_reopen")).find("USB") != std::string::npos, "english watch_usb_reopen");
  check(std::string(t("watch_waiting_reader")).find("waiting") != std::string::npos,
        "english watch_waiting_reader");
  check(set_lang("da"), "set_lang da");
  check(std::string(t("watch_already")).find("kører") != std::string::npos, "danish watch_already");
  check(std::string(t("watch_waiting_reader")).find("venter") != std::string::npos,
        "danish watch_waiting_reader");
  check(std::string(t("restart_ok")).find("genstartet") != std::string::npos, "danish restart_ok");
  check(std::string(t("menu_text")).find("Lyt") != std::string::npos, "danish menu_text");
  check(std::string(t("menu_more_text")).find("Mere") != std::string::npos, "danish menu_more_text");
  check(set_lang("en"), "set_lang en again");
  check(std::string(t("menu_text")).find("Listen") != std::string::npos, "english menu_text");
  check(std::string(t("menu_more_text")).find("More") != std::string::npos, "english menu_more_text");
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
