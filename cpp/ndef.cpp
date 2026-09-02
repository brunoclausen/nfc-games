#include "ndef.hpp"

#include <algorithm>

namespace {

void utf8_pop(std::string& s) {
  while (!s.empty()) {
    const auto c = static_cast<unsigned char>(s.back());
    s.pop_back();
    if ((c & 0xC0) != 0x80) break;
  }
}

std::vector<std::uint8_t> ndef_text_message(std::string_view text, std::string_view lang) {
  std::string lang_s(lang);
  if (lang_s.size() != 2) lang_s = "en";
  const std::size_t payload_len = 1 + lang_s.size() + text.size();
  std::vector<std::uint8_t> msg;
  msg.reserve(4 + payload_len);
  msg.push_back(0xD1);  // MB ME SR TNF=well-known
  msg.push_back(0x01);
  msg.push_back(static_cast<std::uint8_t>(payload_len));
  msg.push_back(0x54);  // 'T'
  msg.push_back(static_cast<std::uint8_t>(lang_s.size() & 0x3F));
  msg.insert(msg.end(), lang_s.begin(), lang_s.end());
  msg.insert(msg.end(), text.begin(), text.end());
  return msg;
}

std::size_t tlv_size_for(std::size_t ndef_len) {
  const std::size_t hdr = (ndef_len >= 0xFF) ? 4u : 2u;
  return hdr + ndef_len + 1;  // terminator
}

std::optional<std::string> parse_ndef_records(const std::uint8_t* p, std::size_t n) {
  std::size_t i = 0;
  while (i + 3 <= n) {
    const std::uint8_t header = p[i];
    const std::uint8_t tnf = header & 0x07;
    const bool sr = (header & 0x10) != 0;
    const bool il = (header & 0x08) != 0;
    const std::uint8_t type_len = p[i + 1];
    std::size_t idx = i + 2;
    std::uint32_t payload_len = 0;
    if (sr) {
      if (idx >= n) break;
      payload_len = p[idx++];
    } else {
      if (idx + 4 > n) break;
      payload_len = (static_cast<std::uint32_t>(p[idx]) << 24) |
                    (static_cast<std::uint32_t>(p[idx + 1]) << 16) |
                    (static_cast<std::uint32_t>(p[idx + 2]) << 8) | p[idx + 3];
      idx += 4;
    }
    std::uint8_t id_len = 0;
    if (il) {
      if (idx >= n) break;
      id_len = p[idx++];
    }
    if (idx + type_len + id_len + payload_len > n) break;
    const std::uint8_t* type = p + idx;
    idx += type_len + id_len;
    const std::uint8_t* payload = p + idx;
    if (tnf == 0x01 && type_len == 1 && type[0] == 'T' && payload_len >= 1) {
      const std::uint8_t lang_len = payload[0] & 0x3F;
      if (1u + lang_len <= payload_len) {
        return std::string(reinterpret_cast<const char*>(payload + 1 + lang_len),
                           payload_len - 1 - lang_len);
      }
    }
    if (tnf == 0x01 && type_len == 2 && type[0] == 'S' && type[1] == 'p' && payload_len > 0) {
      if (auto nested = parse_ndef_records(payload, payload_len)) return nested;
    }
    i = idx + payload_len;
    if (header & 0x40) break;  // ME
  }
  return std::nullopt;
}

}  // namespace

Type2Cc parse_type2_cc(const std::uint8_t page3[4]) {
  Type2Cc cc;
  if (page3[0] != 0xE1) return cc;
  cc.valid = true;
  cc.data_size = static_cast<std::uint16_t>(page3[2]) * 8u;
  cc.writable = (page3[3] & 0x0F) == 0;
  if (cc.data_size < 16) cc.data_size = 16;
  return cc;
}

std::array<std::uint8_t, 4> make_type2_cc(std::uint16_t data_size) {
  std::uint8_t cc2 = static_cast<std::uint8_t>(std::min<std::uint16_t>(data_size / 8, 255));
  if (cc2 == 0) cc2 = 0x12;
  return {0xE1, 0x10, cc2, 0x00};
}

std::uint16_t ntag_user_bytes_from_version(const std::vector<std::uint8_t>& version) {
  if (version.size() < 8) return 144;
  switch (version[6]) {
    case 0x0F:
      return 144;  // NTAG213
    case 0x11:
      return 504;  // NTAG215
    case 0x13:
      return 888;  // NTAG216
    default:
      return 144;
  }
}

std::vector<std::uint8_t> encode_type2_ndef_text(std::string_view text, std::string_view lang,
                                                 std::uint16_t max_user_bytes) {
  if (max_user_bytes < 16) return {};
  std::string body(text);
  auto build = [&]() -> std::vector<std::uint8_t> {
    auto msg = ndef_text_message(body, lang);
    if (msg.size() > 254) return {};
    if (tlv_size_for(msg.size()) > max_user_bytes) return {};
    std::vector<std::uint8_t> out;
    out.reserve(max_user_bytes);
    out.push_back(0x03);
    out.push_back(static_cast<std::uint8_t>(msg.size()));
    out.insert(out.end(), msg.begin(), msg.end());
    out.push_back(0xFE);
    while (out.size() % 4 != 0) out.push_back(0x00);
    return out;
  };
  auto out = build();
  while (out.empty() && !body.empty()) {
    utf8_pop(body);
    out = build();
  }
  return out;
}

std::optional<std::string> decode_type2_ndef_text(const std::vector<std::uint8_t>& user_memory) {
  std::size_t i = 0;
  const std::size_t n = user_memory.size();
  while (i < n) {
    const std::uint8_t type = user_memory[i];
    if (type == 0x00) {
      ++i;
      continue;
    }
    if (type == 0xFE) break;
    if (i + 1 >= n) break;
    std::uint32_t len = user_memory[i + 1];
    std::size_t hdr = 2;
    if (len == 0xFF) {
      if (i + 3 >= n) break;
      len = (static_cast<std::uint32_t>(user_memory[i + 2]) << 8) | user_memory[i + 3];
      hdr = 4;
    }
    if (i + hdr + len > n) break;
    if (type == 0x03) {
      if (len == 0) return std::nullopt;
      return parse_ndef_records(user_memory.data() + i + hdr, len);
    }
    i += hdr + len;
  }
  return std::nullopt;
}
