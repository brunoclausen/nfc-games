#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cstdint>
#include <iomanip>
#include <optional>

#define private public
#include "acr122.hpp"
#undef private

static std::string hex(const std::vector<uint8_t>& v) {
  std::ostringstream os;
  os << std::hex << std::uppercase << std::setfill('0');
  for (uint8_t b : v) os << std::setw(2) << (int)b;
  return os.str();
}

static std::vector<uint8_t> wait_tag(Acr122& r) {
  std::vector<uint8_t> uid;
  while (uid.empty()) {
    uid = r.wait_uid(std::chrono::milliseconds{1000});
  }
  return uid;
}

static int save(const std::string& path) {
  auto r = Acr122::open();
  auto uid = wait_tag(r);
  std::cout << "tag fundet UID " << Acr122::uid_hex(uid) << "\n";

  auto ver = r.ntag_version();
  size_t pages = 0;
  if (ver && ver->size() >= 8) {
    // bytes 6-7 identify product/version; all NTAG variants max 231 pages.
    pages = (*ver)[6] <= 0x03 ? 16 * (*ver)[6] + 8 : 0;  // rough NTAG sizing
  }
  if (pages == 0) pages = 45;  // NTAG213 default
  if (pages > 231) pages = 231;
  std::cout << "type2 pages (est): " << pages << "  version: "
            << (ver ? hex(*ver) : "ukendt") << "\n";

  uint8_t page_bytes[4] = {0, 0, 0, 0};
  std::vector<std::vector<uint8_t>> data;
  for (size_t p = 0; p < pages; ++p) {
    (void)page_bytes;
    auto chunk = r.read_binary(static_cast<uint8_t>(p), 4);
    if (chunk.empty()) break;
    data.push_back(chunk);
  }
  if (data.size() < pages)
    std::cout << "læsning stoppede ved side " << data.size() << "\n";

  std::ofstream f(path);
  f << "UID " << Acr122::uid_hex(uid) << "\n";
  if (ver) f << "VERSION " << hex(*ver) << "\n";
  for (size_t p = 0; p < data.size(); ++p) {
    f << std::setw(2) << std::setfill('0') << std::hex << p << " "
      << hex(data[p]) << "\n";
  }
  std::cout << "backup: " << data.size() << " sider -> " << path << "\n";
  return 0;
}

static int erase() {
  auto r = Acr122::open();
  auto uid = wait_tag(r);
  std::cout << "sletter tag UID " << Acr122::uid_hex(uid) << "\n";
  r.write_ndef_text("");
  auto now = r.read_ndef_text();
  std::cout << "NDEF efter sletning: '"
            << (now ? *now : "(tom)") << "'\n";
  return 0;
}

static int restore(const std::string& path) {
  std::ifstream f(path);
  if (!f) { std::cerr << "kan ikke åbne " << path << "\n"; return 1; }
  std::vector<uint8_t> pages[232];
  std::string line;
  while (std::getline(f, line)) {
    std::istringstream is(line);
    std::string key;
    is >> key;
    if (key == "UID" || key == "VERSION") continue;
    long p = strtol(key.c_str(), nullptr, 16);
    std::string hx;
    is >> hx;
    if (p < 0 || p > 231 || hx.size() % 2) continue;
    for (size_t i = 0; i + 1 < hx.size(); i += 2) {
      pages[p].push_back((uint8_t)strtol(hx.substr(i, 2).c_str(), nullptr, 16));
    }
  }

  auto r = Acr122::open();
  auto uid = wait_tag(r);
  std::cout << "gendanner tag UID " << Acr122::uid_hex(uid) << "\n";
  int ok = 0, fail = 0;
  for (size_t p = 0; p < 232; ++p) {
    if (pages[p].size() != 4) continue;
    if (p <= 2) continue;  // UID/SN0: readonly
    if (pages[p][0] == 0 && pages[p][1] == 0 && pages[p][2] == 0 && pages[p][3] == 0)
      continue;  // skip never-written pages
    uint8_t d[4] = {pages[p][0], pages[p][1], pages[p][2], pages[p][3]};
    try {
      r.write_page(static_cast<uint8_t>(p), d);
      ++ok;
    } catch (const Acr122Error&) {
      ++fail;
    }
  }
  std::cout << "gendannet: " << ok << " sider skrevet, " << fail << " fejlede (forventet for UID/lås)\n";
  auto now = r.read_ndef_text();
  std::cout << "NDEF efter gendannelse: '" << (now ? *now : "(tom)") << "'\n";
  return 0;
}

int main(int argc, char** argv) {
  if (argc < 2) { std::cerr << "brug: tag_tool save <fil> | erase | restore <fil>\n"; return 2; }
  std::string op = argv[1];
  if (op == "save" && argc == 3) return save(argv[2]);
  if (op == "erase") return erase();
  if (op == "restore" && argc == 3) return restore(argv[2]);
  std::cerr << "brug: tag_tool save <fil> | erase | restore <fil>\n";
  return 2;
}