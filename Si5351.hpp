#pragma once
#include <cstdint>
#include <cstddef>
#include <zephyr/drivers/i2c.h>
#include <zephyr/shell/shell.h>

struct LiveChannelStatus {
  bool enabled = false;
  uint8_t driveStrengthmA = 0;
  bool inverted = false;
  bool usesPLLB = false;
  uint32_t decodedFreqHz = 0;
  uint8_t phaseOffsetUnits = 0;
};

struct LiveDeviceStatus {
  uint8_t statusReg0 = 0;
  bool sysInit = false;
  bool lolA = false;
  bool lolB = false;
  bool los = false;
  uint8_t revId = 0;
  uint32_t pllaFreqHz = 0;
  uint32_t pllbFreqHz = 0;
  LiveChannelStatus channels[8];
};

class Si5351 {
public:
  Si5351();
  ~Si5351();

  // Initializes reference parameters and master PLL-A
  bool init(const struct i2c_dt_spec *spec, uint32_t tcxoFreqHz, uint32_t baseFreqHz);

  // Set frequency for specific clock output (0..7)
  bool setFreq(uint8_t clk, uint32_t freqHz);

  // Shifts numerator for specific clock output (0..7) using millihertz metrics
  void tuneWSPROffset(uint8_t clk, int32_t milliHzOffset);

  // Sets static phase offset for specific clock output (0..7)
  void setPhaseOffset(uint8_t clk, uint8_t phaseUnits);

  // Enable/disable specific clock output (0..7)
  void setOutputEnable(uint8_t clk, bool enabled);

  // Reads live hardware registers via burst read and decodes ground truth status
  bool readLiveGroundTruth(LiveDeviceStatus &status);

  // Scans all I2C addresses on the bus and reports responding devices
  void scanI2CBus(const struct shell *sh);

  // Legacy/Global helpers
  bool isInitialized() const { return initialized; }
  void setClockOutputsEnabled(bool enabled);
  void writeRegister(uint8_t reg, uint8_t data);
  uint8_t readRegister(uint8_t reg);
  bool burstRead(uint8_t startReg, uint8_t *buf, size_t len);
  uint8_t getDeviceStatus();
  
  uint32_t getRefFreq() const { return tcxoFreqHz; }
  uint32_t getBaseFreq(uint8_t clk = 0) const;
  uint32_t getPLLFreq() const { return pllFreqHz; }
  int32_t getOffsetMilliHz(uint8_t clk = 0) const;
  bool isOutputEnabled(uint8_t clk);
  uint8_t getDriveStrengthmA(uint8_t clk);

private:
  void updateMultiSynthDividers(uint8_t clk);
  void setupPLLA(uint32_t targetPLLFreqHz, uint32_t refFreqHz);
  void writeSynthParams(uint8_t baseReg, uint32_t multOrDiv, uint32_t num, uint32_t denom, bool divBy4 = false);

  static constexpr uint8_t regDeviceStatus = 0;
  static constexpr uint8_t regOutputControl = 3;
  static constexpr uint8_t regCLKControlBase = 16;
  static constexpr uint8_t regPLLABase = 26;
  static constexpr uint8_t regPLLBBase = 34;
  static constexpr uint8_t regMultiSynthBase = 42;
  static constexpr uint8_t regPhaseOffsetBase = 165;
  static constexpr uint8_t regPLLReset = 177;
  static constexpr uint8_t regCrystalLoad = 183;
  static constexpr uint32_t max20BitValue = 1048575;

  const struct i2c_dt_spec *i2cSpec = nullptr;
  bool initialized = false;

  uint32_t tcxoFreqHz = 0;
  uint32_t pllFreqHz = 900000000;

  uint32_t clkBaseFreqHz[8] = {0};
  int32_t clkOffsetMilliHz[8] = {0};
  uint8_t clkPhaseOffset[8] = {0};

  uint32_t msInteger[8] = {0};
  uint32_t msNumerator[8] = {0};
  uint32_t msDenominator[8] = {0};
};
