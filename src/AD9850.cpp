#include "AD9850.hpp"
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(AD9850, LOG_LEVEL_INF);

AD9850 gDDS;

AD9850::AD9850()
    : gpioDev(nullptr), currentFreqHz(0), refClockHz(125000000),
      currentPhase(0), currentPowerDown(false), initialized(false) {}

bool AD9850::init() {
  gpioDev = DEVICE_DT_GET(DT_NODELABEL(gpio0));
  if (!device_is_ready(gpioDev)) {
    LOG_ERR("GPIO0 device not ready. Cannot initialize AD9850.");
    return false;
  }

  // Configure control and data pins as outputs
  gpio_pin_configure(gpioDev, pinRESET, GPIO_OUTPUT_INACTIVE);
  gpio_pin_configure(gpioDev, pinDATA, GPIO_OUTPUT_INACTIVE);
  gpio_pin_configure(gpioDev, pinWCLK, GPIO_OUTPUT_INACTIVE);
  gpio_pin_configure(gpioDev, pinFQUD, GPIO_OUTPUT_INACTIVE);

  // AD9850 Serial Mode Initialization Sequence:
  // 1. Reset high, then low
  gpio_pin_set(gpioDev, pinRESET, 1);
  k_busy_wait(10); // 10 microseconds
  gpio_pin_set(gpioDev, pinRESET, 0);
  k_busy_wait(10);

  // 2. Pulse WCLK high, then low
  gpio_pin_set(gpioDev, pinWCLK, 1);
  k_busy_wait(1);
  gpio_pin_set(gpioDev, pinWCLK, 0);
  k_busy_wait(1);

  // 3. Pulse FQUD high, then low
  gpio_pin_set(gpioDev, pinFQUD, 1);
  k_busy_wait(1);
  gpio_pin_set(gpioDev, pinFQUD, 0);
  k_busy_wait(1);

  initialized = true;
  LOG_INF("AD9850 initialized in Serial Mode. Pins: RESET=%u, DATA=%u, WCLK=%u, FQUD=%u",
          pinRESET, pinDATA, pinWCLK, pinFQUD);

  // Set default output to 10 MHz
  setFreq(10000000);
  return true;
}

void AD9850::setFreq(uint32_t freqHz) {
  setFreqCustomRef(freqHz, refClockHz);
}

void AD9850::setFreqCustomRef(uint32_t freqHz, uint32_t refClkHz) {
  currentFreqHz = freqHz;
  refClockHz = refClkHz;

  if (refClockHz == 0) return;

  // Calculate 32-bit Frequency Tuning Word (FTW):
  // FTW = (freqHz * 2^32) / refClockHz
  uint64_t numerator = (static_cast<uint64_t>(freqHz) << 32);
  uint32_t tuningWord = static_cast<uint32_t>((numerator + (refClockHz / 2)) / refClockHz);

  write40Bits(tuningWord, currentPhase, currentPowerDown);
}

void AD9850::setPhaseAndPower(uint8_t phaseVal, bool powerDown) {
  currentPhase = phaseVal & 0x1F; // 5 bits (0..31)
  currentPowerDown = powerDown;

  setFreq(currentFreqHz);
}

void AD9850::write40Bits(uint32_t tuningWord, uint8_t phaseVal, bool powerDown) {
  if (!initialized) return;

  // 1. Send the 32-bit Frequency Tuning Word (LSB first)
  for (int i = 0; i < 32; ++i) {
    uint8_t bit = (tuningWord >> i) & 0x01;
    gpio_pin_set(gpioDev, pinDATA, bit);
    gpio_pin_set(gpioDev, pinWCLK, 1);
    // Standard GPIO operations have enough delay, but let's be safe
    gpio_pin_set(gpioDev, pinWCLK, 0);
  }

  // 2. Send the 8-bit Control/Phase byte (LSB first)
  // W32: 0 (test)
  // W33: 0 (test)
  // W34: Power down
  // W35..W39: Phase (5 bits)
  uint8_t controlByte = 0;
  if (powerDown) {
    controlByte |= (1 << 2); // W34 is Bit 2
  }
  controlByte |= ((phaseVal & 0x1F) << 3); // W35..W39 are Bits 3..7

  for (int i = 0; i < 8; ++i) {
    uint8_t bit = (controlByte >> i) & 0x01;
    gpio_pin_set(gpioDev, pinDATA, bit);
    gpio_pin_set(gpioDev, pinWCLK, 1);
    gpio_pin_set(gpioDev, pinWCLK, 0);
  }

  // 3. Pulse FQUD to latch the 40 bits into the active DDS register
  gpio_pin_set(gpioDev, pinFQUD, 1);
  gpio_pin_set(gpioDev, pinFQUD, 0);

  LOG_DBG("AD9850 update: FTW=0x%08X, Phase=%u, PD=%d", tuningWord, phaseVal, powerDown);
}
