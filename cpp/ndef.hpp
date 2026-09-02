#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct Type2Cc {
  bool valid = false;
  bool writable = true;
  std::uint16_t data_size = 0;
};

Type2Cc parse_type2_cc(const std::uint8_t page3[4]);
std::array<std::uint8_t, 4> make_type2_cc(std::uint16_t data_size);

// Type 2 user memory (from page 4): NDEF TLV + terminator, padded to 4 bytes.
std::vector<std::uint8_t> encode_type2_ndef_text(std::string_view text, std::string_view lang,
                                                 std::uint16_t max_user_bytes);

std::optional<std::string> decode_type2_ndef_text(const std::vector<std::uint8_t>& user_memory);

std::uint16_t ntag_user_bytes_from_version(const std::vector<std::uint8_t>& version);
