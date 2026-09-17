#pragma once

#include <string>
#include <vector>

struct EmuGame {
  std::string system;   // short system name, e.g. "ps2" or "gc"
  std::string title;    // SRM parser title, e.g. "Sony PlayStation 2 - PCSX2"
  std::string name;     // display name (file stem)
  std::string path;     // full path to the ROM
  std::string command;  // full shell command used to start it
};

// One Steam ROM Manager parser: a ROM directory, its emulator launcher and
// the file extensions it indexes.
struct SrmParser {
  std::string title;
  std::string rom_dir;
  std::string launcher;
  std::string args;
  std::vector<std::string> exts;  // lowercase, with leading dot
  bool disabled = false;
};

// Extensions inside a SRM glob, e.g. "...@(.iso|.ISO)" -> {".iso"}.
std::vector<std::string> glob_extensions(const std::string& glob);

// Builds a shell command from a launcher, an args template that may contain
// ${filePath}, and the ROM path. Falls back to just the quoted ROM path when
// the template uses variables we cannot resolve.
std::string build_emu_command(const std::string& launcher, const std::string& args,
                              const std::string& file_path);

// Parses `userConfigurations.json`; ${romsdirglobal} and ${/} are expanded.
std::vector<SrmParser> parse_srm_config(const std::string& json, const std::string& roms_dir);

// Installed emulator games from EmuDeck/SRM's ROM tree.
class EmuLibrary {
 public:
  static bool available();
  static EmuLibrary scan();

  const std::vector<EmuGame>& games() const { return games_; }

 private:
  std::vector<EmuGame> games_;
};
