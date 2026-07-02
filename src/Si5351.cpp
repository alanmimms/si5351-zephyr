#include "Si5351.hpp"
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(si5351, LOG_LEVEL_INF);

Si5351::Si5351() {
  tcxoFreqHz = 0;
  pllFreqHz = 883899700;

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

  // 1. Silence all clock outputs during initialization (Reg 3 = 0xFF)
  writeRegister(regOutputControl, 0xFF);

  // 2. Clear tracked state, power down (0x80), and zero out MultiSynth parameters for all 8 channels initially
  for (uint8_t i = 0; i < 8; ++i) {
    clkBaseFreqHz[i] = 0;
    clkOffsetMilliHz[i] = 0;
    clkPhaseOffset[i] = 0;
    msInteger[i] = 0;
    msNumerator[i] = 0;
    msDenominator[i] = max20BitValue;
    writeRegister(regCLKControlBase + i, 0x80);
    writeSynthParams(regMultiSynthBase + (i * 8), 0, 0, max20BitValue);
  }

  // 3. Disable hardware OEB pins so they do not override software output control
  writeRegister(9, 0xFF);

  // 4. Disable internal crystal load capacitance for external TCXO input (0 pF per Skyworks AN619)
  writeRegister(regCrystalLoad, 0x12);

  // 5. Disable spread spectrum
  writeRegister(149, 0x00);

  // 6. Configure master PLL-A to pure integer multiplier 34 (~883.9 MHz) relative to reference frequency
  setupPLLA(pllFreqHz, tcxoFreqHz);

  // 7. Initialize default active channel output CLK0 (strictly 2mA drive strength):
  setFreq(0, baseFreqHz);
  writeRegister(regCLKControlBase + 0, 0x0C);

  // 8. Reset master PLLA once at initialization to align internal accumulators
  writeRegister(regPLLReset, 0x20);

  // 9. Keep all clock outputs initially disabled (Reg 3 = 0xFF)
  writeRegister(regOutputControl, 0xFF);

  initialized = true;
  LOG_INF("Si5351 initialized successfully. Ref=%u Hz, Base=%u Hz, PLL=%u Hz (Pure Integer PLLA)",
          tcxoFreqHz, baseFreqHz, pllFreqHz);
  return true;
}

bool Si5351::setRefFreq(uint32_t refFreqHz) {
  if (!i2cSpec || !i2c_is_ready_dt(i2cSpec) || refFreqHz == 0) return false;

  tcxoFreqHz = refFreqHz;

  // Recalculate pure integer master PLLA settings (b=0)
  setupPLLA(pllFreqHz, tcxoFreqHz);

  // Recalculate integer MultiSynth parameters for active channels
  for (uint8_t clk = 0; clk < 8; ++clk) {
    if (clkBaseFreqHz[clk] > 0) {
      msInteger[clk] = pllFreqHz / clkBaseFreqHz[clk];
      msNumerator[clk] = 0;
      msDenominator[clk] = max20BitValue;
      updateMultiSynthDividers(clk);
    }
  }

  writeRegister(regPLLReset, 0x20);
  LOG_INF("Si5351 reference frequency updated to %u Hz (PLL=%u Hz).", tcxoFreqHz, pllFreqHz);
  return true;
}

void Si5351::setPLLA(uint32_t mult, uint32_t num, uint32_t denom) {
  if (denom == 0) denom = 1;
  uint32_t a = mult;
  uint32_t b = num;
  uint32_t c = denom;

  pllFreqHz = tcxoFreqHz * a + static_cast<uint32_t>((static_cast<uint64_t>(tcxoFreqHz) * b) / c);

  writeSynthParams(regPLLABase, a, b, c);
  writeRegister(regPLLReset, 0x20);

  for (uint8_t clk = 0; clk < 8; ++clk) {
    if (clkBaseFreqHz[clk] > 0) {
      updateMultiSynthDividers(clk);
    }
  }
  LOG_INF("PLLA updated manually to %u + %u/%u (PLL=%u Hz).", a, b, c, pllFreqHz);
}

bool Si5351::setFreq(uint8_t clk, uint32_t freqHz) {
  if (clk > 7 || freqHz == 0) return false;
  clkBaseFreqHz[clk] = freqHz;

  // Pick an even integer MultiSynth divider candidate (multiple of 6) so both clk0/1 (integer a)
  // and clk2/3 (integer a/3) use pure integer MultiSynth dividers with b_ms=0!
  uint32_t bestN = 60;
  uint32_t targetVCO = 850000000;
  uint32_t minDiff = 0xFFFFFFFF;

  for (uint32_t candidate = 12; candidate <= 252; candidate += 6) {
    uint64_t vco = static_cast<uint64_t>(candidate) * freqHz;
    if (vco >= 600000000ULL && vco <= 900000000ULL) {
      uint32_t diff = (vco > targetVCO) ? static_cast<uint32_t>(vco - targetVCO)
                                        : static_cast<uint32_t>(targetVCO - vco);
      if (diff < minDiff) {
        minDiff = diff;
        bestN = candidate;
      }
    }
  }

  uint32_t newPLLFreq = bestN * freqHz;
  bool pllChanged = (newPLLFreq != pllFreqHz);
  pllFreqHz = newPLLFreq;

  // MultiSynth is ALWAYS a pure integer (b=0) to eliminate output stage phase noise spurs
  msInteger[clk] = pllFreqHz / freqHz;
  msNumerator[clk] = 0;
  msDenominator[clk] = max20BitValue;

  // Re-setup PLLA for the exact target PLL frequency (allows fractional PLLA multiplier for exact RF frequency)
  setupPLLA(pllFreqHz, tcxoFreqHz);

  // Configure clock control register for this output
  uint8_t currentCtrl = readRegister(regCLKControlBase + clk);
  uint8_t invertBit = currentCtrl & 0x10;
  uint8_t driveBits = (currentCtrl & 0x80) ? 0x00 : (currentCtrl & 0x03);
  writeRegister(regCLKControlBase + clk, 0x0C | invertBit | driveBits);

  updateMultiSynthDividers(clk);

  // Reset PLL when PLL frequency changes to align accumulators
  if (pllChanged) {
    writeRegister(regPLLReset, 0x20);
  }
  return true;
}

void Si5351::tuneOffset(uint8_t clk, int32_t milliHzOffset) {
  if (clk > 7 || clkBaseFreqHz[clk] == 0) return;
  clkOffsetMilliHz[clk] = milliHzOffset;

  // Modulate PLLA frequency atomically over I2C burst WITHOUT PLL reset!
  // MultiSynth divider msInteger[clk] stays a pure integer (b=0) everywhere to eliminate phase noise spurs.
  int64_t pllOffsetMilliHz = static_cast<int64_t>(msInteger[clk]) * milliHzOffset;
  int64_t targetPLLMilliHz = static_cast<int64_t>(pllFreqHz) * 1000LL + pllOffsetMilliHz;

  if (targetPLLMilliHz <= 0 || tcxoFreqHz == 0) return;

  uint64_t refMilliHz = static_cast<uint64_t>(tcxoFreqHz) * 1000ULL;
  uint32_t a_pll = static_cast<uint32_t>(targetPLLMilliHz / refMilliHz);
  uint64_t rem = static_cast<uint64_t>(targetPLLMilliHz % refMilliHz);
  uint32_t b_pll = static_cast<uint32_t>((rem * max20BitValue) / refMilliHz);
  uint32_t c_pll = max20BitValue;

  writeSynthParams(regPLLABase, a_pll, b_pll, c_pll);
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

bool Si5351::setDriveStrengthmA(uint8_t clk, uint8_t drivemA) {
  if (clk > 7) return false;
  uint8_t bits = 0;
  switch (drivemA) {
    case 2: bits = 0x00; break;
    case 4: bits = 0x01; break;
    case 6: bits = 0x02; break;
    case 8: bits = 0x03; break;
    default: return false;
  }
  uint8_t regAddr = regCLKControlBase + clk;
  uint8_t currentCtrl = readRegister(regAddr);
  currentCtrl = (currentCtrl & ~0x03) | bits;
  writeRegister(regAddr, currentCtrl);
  return true;
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

  struct DecodedSynth {
    double ratio = 0.0;
    uint32_t multOrDiv = 0;
    uint32_t num = 0;
    uint32_t denom = 0;
  };

  auto decodeSynthBlock = [&](uint8_t baseReg) -> DecodedSynth {
    uint32_t p3 = (static_cast<uint32_t>(regs[baseReg + 0]) << 8) | regs[baseReg + 1];
    uint32_t p1High = regs[baseReg + 2] & 0x03;
    uint32_t p1 = (p1High << 16) | (static_cast<uint32_t>(regs[baseReg + 3]) << 8) | regs[baseReg + 4];
    uint32_t p3High = (regs[baseReg + 5] >> 4) & 0x0F;
    p3 |= (p3High << 16);
    uint32_t p2High = regs[baseReg + 5] & 0x0F;
    uint32_t p2 = (p2High << 16) | (static_cast<uint32_t>(regs[baseReg + 6]) << 8) | regs[baseReg + 7];

    if (p3 == 0) p3 = 1;
    DecodedSynth res;
    res.denom = p3;
    uint32_t p1Val = p1 + 512;
    res.multOrDiv = p1Val / 128;
    uint32_t remP1 = p1Val % 128;
    res.num = static_cast<uint32_t>((static_cast<uint64_t>(remP1) * p3 + p2) / 128);
    res.ratio = static_cast<double>(res.multOrDiv) + (static_cast<double>(res.num) / res.denom);
    return res;
  };

  DecodedSynth pllaSynth = decodeSynthBlock(regPLLABase); // Reg 26
  DecodedSynth pllbSynth = decodeSynthBlock(regPLLBBase); // Reg 34

  status.pllaRatio = pllaSynth.ratio;
  status.pllaInteger = pllaSynth.multOrDiv;
  status.pllaNumerator = pllaSynth.num;
  status.pllaDenominator = pllaSynth.denom;

  status.pllaFreqHz = static_cast<uint32_t>(static_cast<double>(tcxoFreqHz) * status.pllaRatio);
  status.pllbFreqHz = static_cast<uint32_t>(static_cast<double>(tcxoFreqHz) * pllbSynth.ratio);

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

    if (poweredUp) {
      DecodedSynth msSynth = decodeSynthBlock(regMultiSynthBase + (c * 8));
      status.channels[c].msRatio = msSynth.ratio;
      status.channels[c].msInteger = msSynth.multOrDiv;
      status.channels[c].msNumerator = msSynth.num;
      status.channels[c].msDenominator = msSynth.denom;

      double pllUsed = status.channels[c].usesPLLB ? status.pllbFreqHz : status.pllaFreqHz;
      if (msSynth.ratio > 0) {
        status.channels[c].decodedFreqHz = static_cast<uint32_t>(pllUsed / msSynth.ratio);
      } else {
        status.channels[c].decodedFreqHz = 0;
      }
    } else {
      status.channels[c].msRatio = 0.0;
      status.channels[c].msInteger = 0;
      status.channels[c].msNumerator = 0;
      status.channels[c].msDenominator = 0;
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
  if (refFreqHz == 0) return;
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
