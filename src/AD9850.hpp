#ifndef AD9850_HPP
#define AD9850_HPP

#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>

class AD9850 {
public:
  AD9850();

  // Initializes the GPIO pins and puts the AD9850 into Serial Write Mode
  bool init();

  // Sets the output frequency in Hz (assuming 125 MHz reference clock by default)
  void setFreq(uint32_t freqHz);

  // Sets frequency with custom reference clock in Hz
  void setFreqCustomRef(uint32_t freqHz, uint32_t refClockHz);

  // Sets phase (0..31 corresponding to 0..360 degrees) and power down state
  void setPhaseAndPower(uint8_t phaseVal, bool powerDown);

  // Getters for status
  uint32_t getFreqHz() const { return currentFreqHz; }
  uint32_t getRefClockHz() const { return refClockHz; }
  uint8_t getPhase() const { return currentPhase; }
  bool isPowerDown() const { return currentPowerDown; }

private:
  void write40Bits(uint32_t tuningWord, uint8_t phaseVal, bool powerDown);

  const struct device *gpioDev;
  uint32_t currentFreqHz;
  uint32_t refClockHz;
  uint8_t currentPhase;
  bool currentPowerDown;
  bool initialized;

  // Pin definitions (ESP32-S3 GPIO pins 10..13)
  static constexpr gpio_pin_t pinRESET = 10;
  static constexpr gpio_pin_t pinDATA = 11;
  static constexpr gpio_pin_t pinWCLK = 12;
  static constexpr gpio_pin_t pinFQUD = 13;
};

// Global instance of the DDS controller
extern AD9850 gDDS;

#endif // AD9850_HPP
