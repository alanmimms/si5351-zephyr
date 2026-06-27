#include "Si5351.hpp"
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(si5351, LOG_LEVEL_INF);

Si5351::Si5351() {
  tcxoFreqHz = 0;
  pllFreqHz = 900000000;

  for (int i = 0; i < 8; ++i) {
    clkBaseFreqHz[i] = 0;
    clkOffsetMilliHz[i] = 0;
    clkPhaseOffset[i] = 0;
    msInteger[i] = 0;
    msNumerator[i] = 0;
    msDenominator[i] = max20BitValue;
  }
}

Si5351::~Si5351() {}

bool Si5351::init(const struct i2c_dt_spec *spec, uint32_t tcxoFreq, uint32_t baseFreqHz) {
  i2cSpec = spec;
  if (!i2cSpec || !i2c_is_ready_dt(i2cSpec)) {
    LOG_ERR("I2C bus device not ready");
    return false;
  }

  tcxoFreqHz = tcxoFreq;

  // 1. Silence all clock outputs during initialization
  writeRegister(regOutputControl, 0xFF);

  // 2. Disable hardware OEB pins so they do not override software output control
  writeRegister(9, 0xFF);

  // 3. Set internal crystal load capacitance (e.g. 10pF)
  writeRegister(regCrystalLoad, 0xD2);

  // 4. Disable spread spectrum
  writeRegister(149, 0x00);

  // 5. Configure master PLL-A to ~900 MHz target relative to reference frequency
  setupPLLA(pllFreqHz, tcxoFreqHz);

  // 6. Initialize default channel outputs (with 8mA drive strength for clean scope signals):
  // CLK0: 1x TX fundamental base frequency (push/pull positive leg)
  setFreq(0, baseFreqHz);

  // CLK1: 1x TX fundamental differential (push/pull negative leg, 180 deg inverted in software)
  setFreq(1, baseFreqHz);
  writeRegister(regCLKControlBase + 1, 0x1F); // PLLA, MS, 8mA, inverted

  // CLK2: 3x AHC tracking loop (push/pull positive leg, 3x base frequency)
  setFreq(2, baseFreqHz * 3);

  // CLK3: 3x AHC tracking loop differential (push/pull negative leg, 180 deg inverted in software)
  setFreq(3, baseFreqHz * 3);
  writeRegister(regCLKControlBase + 3, 0x1F); // PLLA, MS, 8mA, inverted

  // 7. Reset master PLLA once at initialization to align internal accumulators
  writeRegister(regPLLReset, 0x20);

  // 8. Unmask and enable active clock output gates
  setClockOutputsEnabled(true);

  initialized = true;
  LOG_INF("Si5351 initialized successfully. Ref=%u Hz, Base=%u Hz, PLL=%u Hz (All PLLA)",
          tcxoFreqHz, baseFreqHz, pllFreqHz);
  return true;
}

bool Si5351::setFreq(uint8_t clk, uint32_t freqHz) {
  if (clk > 7 || freqHz == 0) return false;
  clkBaseFreqHz[clk] = freqHz;

  msInteger[clk] = pllFreqHz / freqHz;
  uint64_t remainder = pllFreqHz % freqHz;
  msNumerator[clk] = static_cast<uint32_t>((remainder * max20BitValue) / freqHz);
  msDenominator[clk] = max20BitValue;

  // Configure clock control register for this output.
  // Bit 7 = 0 (powered up), Bit 5 = 0 (PLLA), Bit 3:2 = 11 (MS output), Bit 1:0 = 11 (8mA drive) -> 0x0F
  uint8_t currentCtrl = readRegister(regCLKControlBase + clk);
  uint8_t invertBit = currentCtrl & 0x10; // Preserve invert bit (CLK1 / CLK3 push-pull)
  writeRegister(regCLKControlBase + clk, 0x0F | invertBit);

  updateMultiSynthDividers(clk);
  return true;
}

void Si5351::tuneWSPROffset(uint8_t clk, int32_t milliHzOffset) {
  if (clk > 7 || clkBaseFreqHz[clk] == 0) return;
  clkOffsetMilliHz[clk] = milliHzOffset;

  const auto mho = static_cast<int64_t>(milliHzOffset) * max20BitValue;
  const auto cbf = static_cast<int64_t>(clkBaseFreqHz[clk]) * 1000;
  int64_t scaledOffset = mho / cbf;
  uint32_t adjustedNumerator = msNumerator[clk] + static_cast<int32_t>(scaledOffset);

  uint8_t baseReg = regMultiSynthBase + (clk * 8);
  writeSynthParams(baseReg, msInteger[clk], adjustedNumerator, msDenominator[clk]);
}

void Si5351::setPhaseOffset(uint8_t clk, uint8_t phaseUnits) {
  if (clk > 7) return;
  clkPhaseOffset[clk] = phaseUnits;

  setClockOutputsEnabled(false);
  // Assign quantization delay factor directly to target channel phase offset slot (0..127)
  // 1 unit = Tvco / 4 (~278 ps at 900 MHz VCO)
  writeRegister(regPhaseOffsetBase + clk, phaseUnits & 0x7F);

  // Reset PLLA (0x20) to lock phase shift alignment across tracking loops
  writeRegister(regPLLReset, 0x20);
  setClockOutputsEnabled(true);
}

void Si5351::setOutputEnable(uint8_t clk, bool enabled) {
  if (clk > 7) return;
  uint8_t current = readRegister(regOutputControl);
  if (enabled) {
    current &= ~(1 << clk);
  } else {
    current |= (1 << clk);
  }
  writeRegister(regOutputControl, current);
}

void Si5351::setClockOutputsEnabled(bool enabled) {
  writeRegister(regOutputControl, enabled ? 0x00 : 0xFF);
}

void Si5351::writeRegister(uint8_t reg, uint8_t data) {
  if (i2cSpec && i2c_is_ready_dt(i2cSpec)) {
    i2c_reg_write_byte_dt(i2cSpec, reg, data);
  }
}

uint8_t Si5351::readRegister(uint8_t reg) {
  uint8_t data = 0;
  if (i2cSpec && i2c_is_ready_dt(i2cSpec)) {
    i2c_write_read_dt(i2cSpec, &reg, 1, &data, 1);
  }
  return data;
}

bool Si5351::burstRead(uint8_t startReg, uint8_t *buf, size_t len) {
  if (!i2cSpec || !i2c_is_ready_dt(i2cSpec)) return false;

  memset(buf, 0, len);

  // Si5351 valid readable registers are 0..187. Use 16-byte burst reads with single byte fallback.
  for (size_t offset = 0; offset < len && (startReg + offset) <= 187; offset += 16) {
    uint8_t curReg = static_cast<uint8_t>(startReg + offset);
    size_t chunk = (len - offset > 16) ? 16 : (len - offset);
    if (curReg + chunk - 1 > 187) {
      chunk = 187 - curReg + 1;
    }
    if (i2c_write_read_dt(i2cSpec, &curReg, 1, buf + offset, chunk) != 0) {
      for (size_t i = 0; i < chunk; ++i) {
        uint8_t singleReg = curReg + i;
        i2c_write_read_dt(i2cSpec, &singleReg, 1, buf + offset + i, 1);
      }
    }
  }
  return true;
}

void Si5351::scanI2CBus(const struct shell *sh) {
  if (!i2cSpec || !i2c_is_ready_dt(i2cSpec)) {
    if (sh) shell_error(sh, "I2C bus controller is NOT ready!");
    return;
  }

  if (sh) shell_print(sh, "Scanning I2C bus %s (addresses 0x08..0x77)...", i2cSpec->bus->name);
  uint8_t found = 0;
  for (uint8_t addr = 0x08; addr <= 0x77; ++addr) {
    struct i2c_msg msgs[1];
    uint8_t dummy = 0;
    msgs[0].buf = &dummy;
    msgs[0].len = 0;
    msgs[0].flags = I2C_MSG_WRITE | I2C_MSG_STOP;

    int ret = i2c_transfer(i2cSpec->bus, msgs, 1, addr);
    if (ret == 0) {
      if (sh) shell_print(sh, " -> Found I2C device at 0x%02X (%s)",
                          addr, (addr == i2cSpec->addr) ? "Configured Si5351 target" : "Other device");
      found++;
    }
  }
  if (found == 0 && sh) {
    shell_warn(sh, "No I2C devices ACKed on bus! Check SDA/SCL wiring and pullup resistors.");
  }
}

bool Si5351::readLiveGroundTruth(LiveDeviceStatus &status) {
  if (!i2cSpec || !i2c_is_ready_dt(i2cSpec)) return false;

  uint8_t regs[188];
  if (!burstRead(0, regs, sizeof(regs))) {
    return false;
  }

  status.statusReg0 = regs[regDeviceStatus];
  status.sysInit = (status.statusReg0 & (1 << 7)) != 0;
  status.lolB = (status.statusReg0 & (1 << 6)) != 0;
  status.lolA = (status.statusReg0 & (1 << 5)) != 0;
  status.los = (status.statusReg0 & (1 << 4)) != 0;
  status.revId = status.statusReg0 & 0x03;

  auto decodeSynthBlock = [&](uint8_t baseReg) -> double {
    uint32_t p3 = (static_cast<uint32_t>(regs[baseReg + 0]) << 8) | regs[baseReg + 1];
    uint32_t p1High = regs[baseReg + 2] & 0x03;
    uint32_t p1 = (p1High << 16) | (static_cast<uint32_t>(regs[baseReg + 3]) << 8) | regs[baseReg + 4];
    uint32_t p3High = (regs[baseReg + 5] >> 4) & 0x0F;
    p3 |= (p3High << 16);
    uint32_t p2High = regs[baseReg + 5] & 0x0F;
    uint32_t p2 = (p2High << 16) | (static_cast<uint32_t>(regs[baseReg + 6]) << 8) | regs[baseReg + 7];

    if (p3 == 0) p3 = 1;
    double ratio = (static_cast<double>(p1) + 512.0 + (static_cast<double>(p2) / p3)) / 128.0;
    return ratio;
  };

  double pllaRatio = decodeSynthBlock(regPLLABase); // Reg 26
  double pllbRatio = decodeSynthBlock(regPLLBBase); // Reg 34

  status.pllaFreqHz = static_cast<uint32_t>(static_cast<double>(tcxoFreqHz) * pllaRatio);
  status.pllbFreqHz = static_cast<uint32_t>(static_cast<double>(tcxoFreqHz) * pllbRatio);

  uint8_t gateCtrl = regs[regOutputControl]; // Reg 3

  for (uint8_t c = 0; c < 8; ++c) {
    uint8_t clkCtrl = regs[regCLKControlBase + c]; // Reg 16+c
    bool gateUnmasked = (gateCtrl & (1 << c)) == 0;
    bool poweredUp = (clkCtrl & 0x80) == 0;
    status.channels[c].enabled = gateUnmasked && poweredUp;

    uint8_t drvBits = clkCtrl & 0x03;
    switch (drvBits) {
      case 0: status.channels[c].driveStrengthmA = 2; break;
      case 1: status.channels[c].driveStrengthmA = 4; break;
      case 2: status.channels[c].driveStrengthmA = 6; break;
      case 3: status.channels[c].driveStrengthmA = 8; break;
    }

    status.channels[c].inverted = (clkCtrl & 0x10) != 0;
    status.channels[c].usesPLLB = (clkCtrl & 0x20) != 0;
    status.channels[c].phaseOffsetUnits = regs[regPhaseOffsetBase + c] & 0x7F;

    double msRatio = decodeSynthBlock(regMultiSynthBase + (c * 8));
    double pllUsed = status.channels[c].usesPLLB ? status.pllbFreqHz : status.pllaFreqHz;
    if (msRatio > 0) {
      status.channels[c].decodedFreqHz = static_cast<uint32_t>(pllUsed / msRatio);
    } else {
      status.channels[c].decodedFreqHz = 0;
    }
  }

  return true;
}

uint8_t Si5351::getDeviceStatus() {
  return readRegister(regDeviceStatus);
}

uint32_t Si5351::getBaseFreq(uint8_t clk) const {
  return (clk < 8) ? clkBaseFreqHz[clk] : 0;
}

int32_t Si5351::getOffsetMilliHz(uint8_t clk) const {
  return (clk < 8) ? clkOffsetMilliHz[clk] : 0;
}

bool Si5351::isOutputEnabled(uint8_t clk) {
  if (clk > 7) return false;
  uint8_t oeReg = readRegister(regOutputControl);
  uint8_t clkCtrlReg = readRegister(regCLKControlBase + clk);
  bool gateEnabled = (oeReg & (1 << clk)) == 0;
  bool powerUp = (clkCtrlReg & 0x80) == 0;
  return gateEnabled && powerUp;
}

uint8_t Si5351::getDriveStrengthmA(uint8_t clk) {
  if (clk > 7) return 0;
  uint8_t clkCtrlReg = readRegister(regCLKControlBase + clk);
  uint8_t drvBits = clkCtrlReg & 0x03;
  switch (drvBits) {
    case 0: return 2;
    case 1: return 4;
    case 2: return 6;
    case 3: return 8;
    default: return 2;
  }
}

void Si5351::setupPLLA(uint32_t targetPLLFreqHz, uint32_t refFreqHz) {
  uint32_t a = targetPLLFreqHz / refFreqHz;
  uint64_t rem = targetPLLFreqHz % refFreqHz;
  uint32_t b = static_cast<uint32_t>((rem * max20BitValue) / refFreqHz);
  uint32_t c = max20BitValue;

  pllFreqHz = refFreqHz * a + static_cast<uint32_t>((static_cast<uint64_t>(refFreqHz) * b) / c);

  writeSynthParams(regPLLABase, a, b, c);
}

void Si5351::updateMultiSynthDividers(uint8_t clk) {
  if (clk > 7) return;
  uint8_t baseReg = regMultiSynthBase + (clk * 8);
  writeSynthParams(baseReg, msInteger[clk], msNumerator[clk], msDenominator[clk]);
}

void Si5351::writeSynthParams(uint8_t baseReg, uint32_t multOrDiv, uint32_t num, uint32_t denom, bool divBy4) {
  if (denom == 0) denom = 1;

  uint32_t p1 = 128 * multOrDiv + static_cast<uint32_t>((128ULL * num) / denom) - 512;
  uint32_t p2 = static_cast<uint32_t>(128ULL * num - denom * ((128ULL * num) / denom));
  uint32_t p3 = denom;

  uint8_t p1_17_16 = static_cast<uint8_t>((p1 >> 16) & 0x03);
  uint8_t div4Bits = divBy4 ? 0x0C : 0x00;

  uint8_t params[8];
  params[0] = static_cast<uint8_t>((p3 >> 8) & 0xFF);
  params[1] = static_cast<uint8_t>(p3 & 0xFF);
  params[2] = div4Bits | p1_17_16;
  params[3] = static_cast<uint8_t>((p1 >> 8) & 0xFF);
  params[4] = static_cast<uint8_t>(p1 & 0xFF);
  params[5] = static_cast<uint8_t>(((p3 >> 16) & 0x0F) << 4 | ((p2 >> 16) & 0x0F));
  params[6] = static_cast<uint8_t>((p2 >> 8) & 0xFF);
  params[7] = static_cast<uint8_t>(p2 & 0xFF);

  if (i2cSpec && i2c_is_ready_dt(i2cSpec)) {
    i2c_burst_write_dt(i2cSpec, baseReg, params, 8);
  }
}
