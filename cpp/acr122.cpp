#include "acr122.hpp"
#include "i18n.hpp"

#include <libusb.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <iomanip>
#include <optional>
#include <sstream>
#include <thread>

namespace {

constexpr uint16_t kVid = 0x072f;
constexpr uint16_t kPid = 0x2200;

constexpr uint8_t kXfrBlock = 0x6F;
constexpr uint8_t kIccPowerOn = 0x62;
constexpr uint8_t kDataBlock = 0x80;

uint8_t units_100ms(std::chrono::milliseconds ms) {
  const int u = static_cast<int>((ms.count() + 99) / 100);
  return static_cast<uint8_t>(std::clamp(u, 1, 255));
}

uint8_t led_p2_solid(Acr122::Led led) {
  // bits 0-1 final color, bits 2-3 update both, no blinking
  switch (led) {
    case Acr122::Led::Off:
      return 0x0C;
    case Acr122::Led::Red:
      return 0x0D;
    case Acr122::Led::Green:
      return 0x0E;
    case Acr122::Led::Yellow:
      return 0x0F;
  }
  return 0x0C;
}

uint8_t blink_masks(Acr122::Led blink) {
  // bit4 initial red, bit5 initial green, bit6 red blink mask, bit7 green blink mask
  switch (blink) {
    case Acr122::Led::Red:
      return 0x50;  // initial red on, red blink mask
    case Acr122::Led::Green:
      return 0xA0;  // initial green on, green blink mask
    case Acr122::Led::Yellow:
      return 0xF0;  // both
    case Acr122::Led::Off:
      return 0x00;
  }
  return 0x00;
}

std::string usb_err(int rc) { return libusb_strerror(static_cast<libusb_error>(rc)); }

}  // namespace

Acr122 Acr122::open() {
  libusb_context* ctx = nullptr;
  int rc = libusb_init(&ctx);
  if (rc != 0) {
    throw Acr122Error("libusb_init: " + usb_err(rc));
  }

  libusb_device_handle* handle = libusb_open_device_with_vid_pid(ctx, kVid, kPid);
  if (!handle) {
    libusb_exit(ctx);
    throw Acr122Error(t("no_reader"));
  }

  libusb_set_auto_detach_kernel_driver(handle, 1);
  rc = libusb_claim_interface(handle, 0);
  if (rc != 0) {
    libusb_close(handle);
    libusb_exit(ctx);
    throw Acr122Error(t_join("claim_fail", usb_err(rc)) + t("claim_hint"));
  }

  libusb_device* dev = libusb_get_device(handle);
  libusb_config_descriptor* cfg = nullptr;
  rc = libusb_get_active_config_descriptor(dev, &cfg);
  if (rc != 0) {
    libusb_release_interface(handle, 0);
    libusb_close(handle);
    libusb_exit(ctx);
    throw Acr122Error("USB config: " + usb_err(rc));
  }

  uint8_t ep_out = 0x02;
  uint8_t ep_in = 0x82;
  uint16_t max_packet = 64;
  if (cfg->bNumInterfaces > 0 && cfg->interface[0].num_altsetting > 0) {
    const auto& alt = cfg->interface[0].altsetting[0];
    for (int i = 0; i < alt.bNumEndpoints; ++i) {
      const auto& ep = alt.endpoint[i];
      if ((ep.bmAttributes & 0x03) != LIBUSB_TRANSFER_TYPE_BULK) continue;
      if (ep.bEndpointAddress & LIBUSB_ENDPOINT_IN) {
        ep_in = ep.bEndpointAddress;
        max_packet = ep.wMaxPacketSize;
      } else {
        ep_out = ep.bEndpointAddress;
        max_packet = ep.wMaxPacketSize;
      }
    }
  }
  libusb_free_config_descriptor(cfg);

  Acr122 reader(ctx, handle, ep_out, ep_in, max_packet);
  reader.icc_power_on();
  reader.disable_card_detect_buzzer();
  try {
    (void)reader.xfr({0xFF, 0x00, 0x51, 0xFF, 0x00}, 1000);
  } catch (const Acr122Error&) {
  }
  try {
    // PN532 SAMConfiguration: normal mode
    (void)reader.xfr({0xFF, 0x00, 0x00, 0x00, 0x05, 0xD4, 0x14, 0x01, 0x00, 0x00}, 1000);
  } catch (const Acr122Error&) {
  }
  try {
    // RF field on
    (void)reader.xfr({0xFF, 0x00, 0x00, 0x00, 0x04, 0xD4, 0x32, 0x01, 0x01}, 1000);
  } catch (const Acr122Error&) {
  }
  return reader;
}

Acr122::Acr122(libusb_context* ctx, libusb_device_handle* handle, uint8_t ep_out,
               uint8_t ep_in, uint16_t max_packet)
    : ctx_(ctx),
      handle_(handle),
      ep_out_(ep_out),
      ep_in_(ep_in),
      max_packet_(max_packet) {}

Acr122::Acr122(Acr122&& other) noexcept { *this = std::move(other); }

Acr122& Acr122::operator=(Acr122&& other) noexcept {
  if (this == &other) return *this;
  close();
  ctx_ = other.ctx_;
  handle_ = other.handle_;
  ep_out_ = other.ep_out_;
  ep_in_ = other.ep_in_;
  max_packet_ = other.max_packet_;
  seq_ = other.seq_;
  other.ctx_ = nullptr;
  other.handle_ = nullptr;
  return *this;
}

void Acr122::close() {
  if (handle_) {
    libusb_release_interface(handle_, 0);
    libusb_close(handle_);
    handle_ = nullptr;
  }
  if (ctx_) {
    libusb_exit(ctx_);
    ctx_ = nullptr;
  }
}

Acr122::~Acr122() { close(); }

void Acr122::bulk_write(const std::vector<uint8_t>& data, int timeout_ms) {
  int transferred = 0;
  int rc = libusb_bulk_transfer(handle_, ep_out_,
                                const_cast<uint8_t*>(data.data()),
                                static_cast<int>(data.size()), &transferred,
                                timeout_ms);
  if (rc != 0) {
    if (rc == LIBUSB_ERROR_TIMEOUT) drain();
    throw Acr122Error("USB write: " + usb_err(rc));
  }
  if (max_packet_ > 0 && (data.size() % max_packet_) == 0) {
    libusb_bulk_transfer(handle_, ep_out_, nullptr, 0, &transferred, timeout_ms);
  }
}

std::vector<uint8_t> Acr122::bulk_read(int timeout_ms) {
  std::array<uint8_t, 271> buf{};
  int transferred = 0;
  int rc = libusb_bulk_transfer(handle_, ep_in_, buf.data(),
                                static_cast<int>(buf.size()), &transferred,
                                timeout_ms);
  if (rc != 0) {
    if (rc == LIBUSB_ERROR_TIMEOUT) drain();
    throw Acr122Error("USB read: " + usb_err(rc));
  }
  if (transferred < 10) {
    throw Acr122Error(t("ccid_short"));
  }
  return {buf.begin(), buf.begin() + transferred};
}

void Acr122::drain() {
  if (!handle_) return;
  std::array<uint8_t, 271> buf{};
  int transferred = 0;
  for (int i = 0; i < 8; ++i) {
    const int rc = libusb_bulk_transfer(handle_, ep_in_, buf.data(),
                                        static_cast<int>(buf.size()), &transferred, 30);
    if (rc != 0) break;
  }
}

void Acr122::flush() { drain(); }

void Acr122::icc_power_on() {
  std::vector<uint8_t> frame = {kIccPowerOn, 0, 0, 0, 0, 0, seq_++, 0x01, 0, 0};
  try {
    bulk_write(frame, 1000);
    (void)bulk_read(1000);
  } catch (const Acr122Error&) {
    drain();
    // LED/buzzer APDUs still work without a card in the field.
  }
}

void Acr122::recover() {
  drain();
  try {
    icc_power_on();
  } catch (const Acr122Error&) {
  }
  try {
    (void)xfr({0xFF, 0x00, 0x51, 0xFF, 0x00}, 1000);
  } catch (const Acr122Error&) {
  }
  try {
    (void)xfr({0xFF, 0x00, 0x00, 0x00, 0x05, 0xD4, 0x14, 0x01, 0x00, 0x00}, 1000);
  } catch (const Acr122Error&) {
  }
  try {
    (void)xfr({0xFF, 0x00, 0x00, 0x00, 0x04, 0xD4, 0x32, 0x01, 0x01}, 1000);
  } catch (const Acr122Error&) {
  }
}

Acr122::ApduReply Acr122::transmit(const std::vector<uint8_t>& apdu, int timeout_ms) {
  const uint32_t len = static_cast<uint32_t>(apdu.size());
  std::vector<uint8_t> frame;
  frame.reserve(10 + apdu.size());
  frame.push_back(kXfrBlock);
  frame.push_back(static_cast<uint8_t>(len));
  frame.push_back(static_cast<uint8_t>(len >> 8));
  frame.push_back(static_cast<uint8_t>(len >> 16));
  frame.push_back(static_cast<uint8_t>(len >> 24));
  frame.push_back(0x00);
  frame.push_back(seq_++);
  frame.push_back(0x00);
  frame.push_back(0x00);
  frame.push_back(0x00);
  frame.insert(frame.end(), apdu.begin(), apdu.end());

  bulk_write(frame, timeout_ms);
  auto rx = bulk_read(timeout_ms);
  if (rx[0] != kDataBlock) {
    throw Acr122Error(t("ccid_unexpected"));
  }
  const uint8_t ccid_status = rx[7];
  const uint8_t ccid_error = rx[8];
  if (ccid_error == 0xFE) {
    return {{}, 0x63, 0x00};
  }
  const uint32_t payload_len =
      static_cast<uint32_t>(rx[1]) | (static_cast<uint32_t>(rx[2]) << 8) |
      (static_cast<uint32_t>(rx[3]) << 16) | (static_cast<uint32_t>(rx[4]) << 24);
  if (rx.size() < 10 + payload_len) {
    throw Acr122Error(t("ccid_trunc"));
  }
  std::vector<uint8_t> payload(rx.begin() + 10, rx.begin() + 10 + payload_len);
  ApduReply reply;
  if (payload.size() >= 2) {
    const uint8_t sw1 = payload[payload.size() - 2];
    const uint8_t sw2 = payload[payload.size() - 1];
    if (sw1 == 0x90 || sw1 == 0x63 || sw1 == 0x61 || sw1 == 0x6A || sw1 == 0x6F) {
      reply.sw1 = sw1;
      reply.sw2 = sw2;
      payload.resize(payload.size() - 2);
      reply.data = std::move(payload);
    } else {
      reply.data = std::move(payload);
      reply.sw1 = ((ccid_status & 0xC0) == 0) ? 0x90 : 0x6F;
      reply.sw2 = ccid_error;
    }
  } else if ((ccid_status & 0xC0) == 0) {
    reply.data = std::move(payload);
    reply.sw1 = 0x90;
  } else {
    reply.sw1 = 0x6F;
    reply.sw2 = ccid_error;
  }
  if (reply.sw1 == 0x61) {
    auto more = transmit({0xFF, 0xC0, 0x00, 0x00, reply.sw2}, timeout_ms);
    reply.data.insert(reply.data.end(), more.data.begin(), more.data.end());
    reply.sw1 = more.sw1;
    reply.sw2 = more.sw2;
  }
  return reply;
}

std::vector<uint8_t> Acr122::xfr(const std::vector<uint8_t>& apdu, int timeout_ms) {
  auto r = transmit(apdu, timeout_ms);
  if (r.sw1 == 0x90) return r.data;
  std::ostringstream os;
  os << std::hex << t("apdu_fail") << static_cast<int>(r.sw1) << " "
     << static_cast<int>(r.sw2);
  throw Acr122Error(os.str());
}

std::string Acr122::uid_hex(const std::vector<uint8_t>& uid) {
  std::ostringstream os;
  os << std::hex << std::uppercase << std::setfill('0');
  for (uint8_t b : uid) os << std::setw(2) << static_cast<int>(b);
  return os.str();
}

std::optional<std::vector<uint8_t>> Acr122::parse_inlist(const std::vector<uint8_t>& data) {
  const uint8_t needle[] = {0xD5, 0x4B};
  auto it = std::search(data.begin(), data.end(), std::begin(needle), std::end(needle));
  if (it == data.end()) return std::nullopt;
  const size_t i = static_cast<size_t>(it - data.begin());
  if (i + 3 > data.size()) return std::nullopt;
  const uint8_t nbtg = data[i + 2];
  if (nbtg < 1) return std::nullopt;
  if (i + 8 > data.size()) return std::nullopt;
  const uint8_t uid_len = data[i + 7];
  if (uid_len < 4 || i + 8 + uid_len > data.size()) return std::nullopt;
  return std::vector<uint8_t>(data.begin() + i + 8, data.begin() + i + 8 + uid_len);
}

std::optional<std::vector<uint8_t>> Acr122::try_uid(bool reactivate) {
  try {
    if (reactivate) icc_power_on();
    const int t_ms = reactivate ? 1000 : 400;
    auto r = transmit({0xFF, 0xCA, 0x00, 0x00, 0x00}, t_ms);
    if (r.sw1 == 0x90 && r.data.size() >= 4) return r.data;
  } catch (const Acr122Error&) {
    drain();
    throw;
  }
  try {
    auto r = transmit({0xFF, 0x00, 0x00, 0x00, 0x04, 0xD4, 0x4A, 0x01, 0x00}, 1500);
    if (r.sw1 == 0x90) {
      if (auto uid = parse_inlist(r.data)) return uid;
      if (r.data.size() >= 4 && r.data[0] != 0xD5) return r.data;
    }
  } catch (const Acr122Error&) {
    drain();
    throw;
  }
  return std::nullopt;
}

std::vector<uint8_t> Acr122::wait_uid(std::chrono::milliseconds timeout) {
  using clock = std::chrono::steady_clock;
  const bool forever = timeout.count() <= 0;
  const auto deadline = clock::now() + timeout;
  set_led(Led::Green);
  while (forever || clock::now() < deadline) {
    if (auto uid = try_uid()) {
      try {
        set_led(Led::Yellow);
        beep(std::chrono::milliseconds{150}, 2);
      } catch (const Acr122Error&) {
      }
      return *uid;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{200});
  }
  set_led(Led::Red);
  throw Acr122Error(t("timeout_tag"));
}

void Acr122::disable_card_detect_buzzer() {
  // FF 00 52 00 00 — buzzer off when a tag is detected
  try {
    (void)xfr({0xFF, 0x00, 0x52, 0x00, 0x00}, 1000);
  } catch (const Acr122Error&) {
  }
}

std::string Acr122::firmware() {
  auto data = xfr({0xFF, 0x00, 0x48, 0x00, 0x00}, 1000);
  return {data.begin(), data.end()};
}

void Acr122::set_led(Led led) {
  // FF 00 40 P2 04 T1 T2 N Link — solid color, no beep
  (void)xfr({0xFF, 0x00, 0x40, led_p2_solid(led), 0x04, 0x00, 0x00, 0x00, 0x00},
            1000);
}

void Acr122::beep(std::chrono::milliseconds duration, int times) {
  times = std::clamp(times, 1, 255);
  const uint8_t t1 = units_100ms(duration);
  // T2 is the gap between beeps; 0 for a single pulse.
  const uint8_t t2 =
      (times > 1) ? units_100ms(std::chrono::milliseconds{100}) : 0;
  // Keep current LED; buzzer during T1. Number of repetition must be > 0.
  const int timeout =
      static_cast<int>((duration.count() + (times > 1 ? 100 : 0)) * times) + 1500;
  (void)xfr({0xFF, 0x00, 0x40, 0x00, 0x04, t1, t2, static_cast<uint8_t>(times), 0x01},
            timeout);
}

void Acr122::blink(Led blink_led, Led final, std::chrono::milliseconds on,
                   std::chrono::milliseconds off, int repeats, bool buzz) {
  repeats = std::clamp(repeats, 1, 255);
  const uint8_t t1 = units_100ms(on);
  const uint8_t t2 = units_100ms(off);
  const uint8_t p2 = static_cast<uint8_t>(led_p2_solid(final) | blink_masks(blink_led));
  const uint8_t link = buzz ? 0x01 : 0x00;
  const int timeout =
      static_cast<int>((on.count() + off.count()) * repeats) + 1500;
  (void)xfr({0xFF, 0x00, 0x40, p2, 0x04, t1, t2, static_cast<uint8_t>(repeats), link},
            timeout);
}
