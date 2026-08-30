#include "acr122.hpp"

#include <libusb.h>

#include <algorithm>
#include <array>
#include <sstream>

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
    throw Acr122Error("Ingen ACR122U fundet (USB 072f:2200). Er læseren sat i?");
  }

  libusb_set_auto_detach_kernel_driver(handle, 1);
  rc = libusb_claim_interface(handle, 0);
  if (rc != 0) {
    libusb_close(handle);
    libusb_exit(ctx);
    throw Acr122Error("Kunne ikke claim USB-interface: " + usb_err(rc) +
                      " (kør med sudo eller installer udev-reglen)");
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
    throw Acr122Error("USB read: " + usb_err(rc));
  }
  if (transferred < 10) {
    throw Acr122Error("CCID-svar for kort");
  }
  return {buf.begin(), buf.begin() + transferred};
}

void Acr122::icc_power_on() {
  std::vector<uint8_t> frame = {kIccPowerOn, 0, 0, 0, 0, 0, seq_++, 0x01, 0, 0};
  try {
    bulk_write(frame, 1000);
    (void)bulk_read(1000);
  } catch (const Acr122Error&) {
    // LED/buzzer APDUs still work without a card in the field.
  }
}

std::vector<uint8_t> Acr122::xfr(const std::vector<uint8_t>& apdu, int timeout_ms) {
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
    throw Acr122Error("Uventet CCID-svar");
  }
  const uint32_t payload_len =
      static_cast<uint32_t>(rx[1]) | (static_cast<uint32_t>(rx[2]) << 8) |
      (static_cast<uint32_t>(rx[3]) << 16) | (static_cast<uint32_t>(rx[4]) << 24);
  if (rx.size() < 10 + payload_len) {
    throw Acr122Error("CCID payload afkortet");
  }
  std::vector<uint8_t> payload(rx.begin() + 10, rx.begin() + 10 + payload_len);
  const uint8_t ccid_status = rx[7];
  const uint8_t ccid_error = rx[8];
  auto hexdump = [](const std::vector<uint8_t>& v) {
    std::ostringstream os;
    os << std::hex;
    for (size_t i = 0; i < v.size(); ++i) {
      if (i) os << ' ';
      os.width(2);
      os.fill('0');
      os << static_cast<int>(v[i]);
    }
    return os.str();
  };
  if (payload.size() >= 2 && payload[payload.size() - 2] == 0x90) {
    payload.resize(payload.size() - 2);
    return payload;
  }
  // Some ACR122 firmware replies (e.g. Get Firmware) omit ISO SW1/SW2.
  if ((ccid_status & 0xC0) == 0 && !payload.empty()) {
    return payload;
  }
  std::ostringstream os;
  os << "APDU fejlede ccid_status=" << std::hex << static_cast<int>(ccid_status)
     << " err=" << static_cast<int>(ccid_error) << " data=[" << hexdump(payload)
     << "] raw=[" << hexdump(rx) << "]";
  throw Acr122Error(os.str());
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

void Acr122::beep(std::chrono::milliseconds duration) {
  const uint8_t t1 = units_100ms(duration);
  // Keep current LED; beep during T1. Number of repetition must be > 0.
  const int timeout = static_cast<int>(duration.count()) + 1500;
  (void)xfr({0xFF, 0x00, 0x40, 0x00, 0x04, t1, 0x00, 0x01, 0x01}, timeout);
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
