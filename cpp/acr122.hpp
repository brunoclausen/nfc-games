#pragma once

#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

struct libusb_context;
struct libusb_device_handle;

class Acr122Error : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

// ACR122U over USB CCID (no pcscd, no libnfc).
class Acr122 {
 public:
  enum class Led { Off, Red, Green, Yellow };

  static Acr122 open();

  Acr122(const Acr122&) = delete;
  Acr122& operator=(const Acr122&) = delete;
  Acr122(Acr122&& other) noexcept;
  Acr122& operator=(Acr122&& other) noexcept;
  ~Acr122();

  std::string firmware();
  void set_led(Led led);
  void beep(std::chrono::milliseconds duration = std::chrono::milliseconds{200});
  // Blink then leave `final` on. Buzzer sounds during the on-phase when buzz=true.
  void blink(Led blink_led, Led final, std::chrono::milliseconds on,
             std::chrono::milliseconds off, int repeats, bool buzz);

 private:
  Acr122(libusb_context* ctx, libusb_device_handle* handle, uint8_t ep_out,
         uint8_t ep_in, uint16_t max_packet);
  void close();

  std::vector<uint8_t> xfr(const std::vector<uint8_t>& apdu, int timeout_ms);
  void icc_power_on();
  void disable_card_detect_buzzer();
  void bulk_write(const std::vector<uint8_t>& data, int timeout_ms);
  std::vector<uint8_t> bulk_read(int timeout_ms);

  libusb_context* ctx_ = nullptr;
  libusb_device_handle* handle_ = nullptr;
  uint8_t ep_out_ = 0;
  uint8_t ep_in_ = 0;
  uint16_t max_packet_ = 64;
  uint8_t seq_ = 0;
};
