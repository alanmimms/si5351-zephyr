#pragma once

#include <cstdint>

/**
 * @brief Base class for JT/WSPR encoders.
 */
template<uint16_t TONE_SPACING, uint16_t SYMBOL_PERIOD_MS, uint32_t DEFAULT_FREQ, int TX_BUFFER_SIZE>
class JTEncoder {
public:
  static constexpr uint16_t toneSpacing = TONE_SPACING;
  static constexpr uint16_t symbolPeriod = SYMBOL_PERIOD_MS;
  static constexpr int txBufferSize = TX_BUFFER_SIZE;

  uint32_t txFreq;
  uint8_t symbols[txBufferSize];

  explicit JTEncoder(uint32_t frequency = DEFAULT_FREQ) : txFreq(frequency), symbols{} {}
  virtual ~JTEncoder() = default;

  virtual void encode(const char* callsign, const char* locator, int8_t powerDbm) {}
  virtual void encode(const char* message) {}

protected:
  virtual void packBits() {}
  virtual void packBits(const char* message) {}
  virtual void computeFec() {}
  virtual void interleave() {}
  virtual void generateSync() {}
  virtual void convolveSymbols() {}

  uint8_t packedData[32];
};

class WSPREncoder : public JTEncoder<146, 683, 14097000UL + 1500, 162> {
public:
  using JTEncoder::JTEncoder;
  void encode(const char* callsign, const char* locator, int8_t powerDbm) override;

private:
  void packBits() override;
  void convolveSymbols() override;
  void interleave() override;

  char callsign[12];
  char locator[7];
  int8_t powerDbm;
};
