#include "shell-commands.hpp"
#include "AD9850.hpp"
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <stdlib.h>
#include <stdio.h>

static int cmdInit(const struct shell *sh, size_t argc, char **argv) {
  uint32_t refHz = 25997050;
  uint32_t baseHz = 14095600;

  if (argc > 1) {
    refHz = strtoul(argv[1], NULL, 0);
  }
  if (argc > 2) {
    baseHz = strtoul(argv[2], NULL, 0);
  }

  shell_print(sh, "Initializing Si5351 (Ref: %.6f MHz, Base: %.6f MHz)...",
              static_cast<double>(refHz) / 1000000.0, static_cast<double>(baseHz) / 1000000.0);
  if (gSi5351.init(&gI2CSpec, refHz, baseHz)) {
    shell_print(sh, "Si5351 initialization SUCCESS.");
  } else {
    shell_error(sh, "Si5351 initialization FAILED (Check I2C connections/address).");
    return -EIO;
  }
  return 0;
}

static int cmdFreq(const struct shell *sh, size_t argc, char **argv) {
  if (argc < 3) {
    shell_error(sh, "Usage: si5351 freq <clk_0_to_7> <freq_hz>");
    return -EINVAL;
  }
  uint8_t clk = static_cast<uint8_t>(strtoul(argv[1], NULL, 0));
  uint32_t freqHz = strtoul(argv[2], NULL, 0);

  if (clk > 7) {
    shell_error(sh, "Invalid clock output channel: %u (must be 0..7)", clk);
    return -EINVAL;
  }

  shell_print(sh, "Setting CLK%u frequency to %.6f MHz...", clk, static_cast<double>(freqHz) / 1000000.0);
  if (gSi5351.setFreq(clk, freqHz)) {
    shell_print(sh, "CLK%u frequency updated.", clk);
  } else {
    shell_error(sh, "Failed to set CLK%u frequency.", clk);
    return -EIO;
  }
  return 0;
}

static int cmdOffset(const struct shell *sh, size_t argc, char **argv) {
  if (argc < 3) {
    shell_error(sh, "Usage: si5351 offset <clk_0_to_7> <milliHz_offset>");
    return -EINVAL;
  }
  uint8_t clk = static_cast<uint8_t>(strtoul(argv[1], NULL, 0));
  int32_t mhz = strtol(argv[2], NULL, 0);

  if (clk > 7) {
    shell_error(sh, "Invalid clock output channel: %u (must be 0..7)", clk);
    return -EINVAL;
  }

  shell_print(sh, "Tuning CLK%u offset to %d milliHz via phase-continuous atomic PLLA burst...", clk, mhz);
  gSi5351.tuneOffset(clk, mhz);
  return 0;
}

static int cmdPhase(const struct shell *sh, size_t argc, char **argv) {
  if (argc < 3) {
    shell_error(sh, "Usage: si5351 phase <clk_0_to_7> <units_0_to_127>");
    return -EINVAL;
  }
  uint8_t clk = static_cast<uint8_t>(strtoul(argv[1], NULL, 0));
  uint8_t phase = static_cast<uint8_t>(strtoul(argv[2], NULL, 0));

  if (clk > 7) {
    shell_error(sh, "Invalid clock output channel: %u (must be 0..7)", clk);
    return -EINVAL;
  }

  shell_print(sh, "Setting CLK%u phase offset to %u units...", clk, phase);
  gSi5351.setPhaseOffset(clk, phase);
  return 0;
}

static int cmdEnable(const struct shell *sh, size_t argc, char **argv) {
  if (argc < 3) {
    shell_error(sh, "Usage: si5351 enable <clk_0_to_7> <0|1>");
    return -EINVAL;
  }
  uint8_t clk = static_cast<uint8_t>(strtoul(argv[1], NULL, 0));
  bool en = strtoul(argv[2], NULL, 0) != 0;

  if (clk > 7) {
    shell_error(sh, "Invalid clock output channel: %u (must be 0..7)", clk);
    return -EINVAL;
  }

  gSi5351.setOutputEnable(clk, en);
  shell_print(sh, "CLK%u output %s", clk, en ? "ENABLED" : "DISABLED");
  return 0;
}

static int cmdDrive(const struct shell *sh, size_t argc, char **argv) {
  if (argc < 3) {
    shell_error(sh, "Usage: si5351 drive <clk_0_to_7> <2|4|6|8>");
    return -EINVAL;
  }
  uint8_t clk = static_cast<uint8_t>(strtoul(argv[1], NULL, 0));
  uint8_t drv = static_cast<uint8_t>(strtoul(argv[2], NULL, 0));

  if (clk > 7) {
    shell_error(sh, "Invalid clock output channel: %u (must be 0..7)", clk);
    return -EINVAL;
  }

  if (!gSi5351.setDriveStrengthmA(clk, drv)) {
    shell_error(sh, "Invalid drive strength: %umA (must be 2, 4, 6, or 8)", drv);
    return -EINVAL;
  }

  shell_print(sh, "CLK%u drive strength set to %umA", clk, drv);
  return 0;
}

static int cmdStatus(const struct shell *sh, size_t argc, char **argv) {
  LiveDeviceStatus live;
  if (!gSi5351.readLiveGroundTruth(live)) {
    shell_error(sh, "Failed to read live hardware ground truth from Si5351 over I2C.");
    return -EIO;
  }

  shell_print(sh, "--- Si5351 Live Ground Truth Status ---");
  shell_print(sh, "Status Register (0): 0x%02X", live.statusReg0);
  shell_print(sh, "  SYS_INIT: %s", live.sysInit ? "In progress" : "Ready");
  shell_print(sh, "  LOL_B:    %s", live.lolB ? "Unlocked" : "Locked");
  shell_print(sh, "  LOL_A:    %s", live.lolA ? "Unlocked" : "Locked");
  shell_print(sh, "  LOS:      %s", live.los ? "Loss of signal" : "Normal");
  shell_print(sh, "  REV_ID:   0x%X", live.revId);

  shell_print(sh, "Ref Freq:  %.6f MHz (%u Hz)", static_cast<double>(gSi5351.getRefFreq()) / 1000000.0, gSi5351.getRefFreq());
  shell_print(sh, "PLLA Freq: %.6f MHz (Mult: %u + %u/%u, Ratio: %.6f)",
              static_cast<double>(live.pllaFreqHz) / 1000000.0,
              live.pllaInteger, live.pllaNumerator, live.pllaDenominator, live.pllaRatio);

  if (argc > 1) {
    uint8_t clk = static_cast<uint8_t>(strtoul(argv[1], NULL, 0));
    if (clk < 8) {
      const auto &ch = live.channels[clk];
      shell_print(sh, "CLK%u Live: Freq=%.6f MHz, Offset=%d mHz, State=%s, Drive=%umA, Phase=%u, PLL=%s, Inv=%s",
                  clk, static_cast<double>(ch.decodedFreqHz) / 1000000.0,
                  gSi5351.getOffsetMilliHz(clk), ch.enabled ? "ENABLED" : "DISABLED",
                  ch.driveStrengthmA, ch.phaseOffsetUnits, ch.usesPLLB ? "PLLB" : "PLLA",
                  ch.inverted ? "YES" : "NO");
      shell_print(sh, "  MS Divider: %u + %u/%u (Ratio: %.6f)",
                  ch.msInteger, ch.msNumerator, ch.msDenominator, ch.msRatio);
    }
  } else {
    shell_print(sh, "Active Channels Live Config:");
    for (uint8_t c = 0; c < 8; ++c) {
      const auto &ch = live.channels[c];
      if (ch.decodedFreqHz > 0 || ch.enabled) {
        shell_print(sh, "  CLK%u: Freq=%.6f MHz, Offset=%d mHz, State=%s, Drive=%umA, Phase=%u, PLL=%s, Inv=%s",
                    c, static_cast<double>(ch.decodedFreqHz) / 1000000.0,
                    gSi5351.getOffsetMilliHz(c), ch.enabled ? "ENABLED" : "DISABLED",
                    ch.driveStrengthmA, ch.phaseOffsetUnits, ch.usesPLLB ? "PLLB" : "PLLA",
                    ch.inverted ? "YES" : "NO");
        shell_print(sh, "        MS Divider: %u + %u/%u (Ratio: %.6f)",
                    ch.msInteger, ch.msNumerator, ch.msDenominator, ch.msRatio);
      }
    }
  }
  return 0;
}

static int cmdRegRead(const struct shell *sh, size_t argc, char **argv) {
  if (argc < 2) {
    shell_error(sh, "Usage: si5351 reg_read <reg_addr>");
    return -EINVAL;
  }
  uint8_t reg = static_cast<uint8_t>(strtoul(argv[1], NULL, 0));
  uint8_t val = gSi5351.readRegister(reg);
  shell_print(sh, "Reg [0x%02X (%u)] = 0x%02X (%u)", reg, reg, val, val);
  return 0;
}

static int cmdRegWrite(const struct shell *sh, size_t argc, char **argv) {
  if (argc < 3) {
    shell_error(sh, "Usage: si5351 reg_write <reg_addr> <val>");
    return -EINVAL;
  }
  uint8_t reg = static_cast<uint8_t>(strtoul(argv[1], NULL, 0));
  uint8_t val = static_cast<uint8_t>(strtoul(argv[2], NULL, 0));
  gSi5351.writeRegister(reg, val);
  shell_print(sh, "Wrote 0x%02X to Reg [0x%02X]", val, reg);
  return 0;
}

static int cmdScan(const struct shell *sh, size_t argc, char **argv) {
  ARG_UNUSED(argc);
  ARG_UNUSED(argv);
  gSi5351.scanI2CBus(sh);
  return 0;
}

static int cmdDump(const struct shell *sh, size_t argc, char **argv) {
  ARG_UNUSED(argc);
  ARG_UNUSED(argv);

  uint8_t regs[256];
  if (!gSi5351.burstRead(0, regs, sizeof(regs))) {
    shell_error(sh, "Failed to burst read Si5351 registers.");
    return -EIO;
  }

  shell_print(sh, "--- Si5351 Register Map Dump (256 bytes) ---");
  shell_print(sh, "   00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F");
  char line[128];
  for (int row = 0; row < 16; ++row) {
    int pos = snprintf(line, sizeof(line), "%02X: ", row * 16);
    for (int col = 0; col < 16; ++col) {
      pos += snprintf(line + pos, sizeof(line) - pos, "%02X ", regs[row * 16 + col]);
    }
    shell_print(sh, "%s", line);
  }
  return 0;
}

static int cmdRef(const struct shell *sh, size_t argc, char **argv) {
  if (argc < 2) {
    shell_print(sh, "Current reference clock frequency: %u Hz (%.6f MHz)",
                gSi5351.getRefFreq(), static_cast<double>(gSi5351.getRefFreq()) / 1000000.0);
    shell_print(sh, "Usage to set: si5351 ref <ref_hz_in_hz>");
    return 0;
  }
  uint32_t refHz = strtoul(argv[1], NULL, 0);
  if (refHz == 0) {
    shell_error(sh, "Invalid reference frequency in Hz.");
    return -EINVAL;
  }

  shell_print(sh, "Updating reference clock frequency to %u Hz (%.6f MHz)...",
              refHz, static_cast<double>(refHz) / 1000000.0);
  if (gSi5351.setRefFreq(refHz)) {
    shell_print(sh, "Reference frequency updated atomically. Channel output states preserved.");
  } else {
    shell_error(sh, "Failed to update reference frequency.");
    return -EIO;
  }
  return 0;
}

static int cmdPLL(const struct shell *sh, size_t argc, char **argv) {
  if (argc < 3) {
    shell_error(sh, "Usage: si5351 pll <mult_int> <num> [denom (default 1048575)]");
    return -EINVAL;
  }
  uint32_t mult = strtoul(argv[1], NULL, 0);
  uint32_t num = strtoul(argv[2], NULL, 0);
  uint32_t denom = (argc > 3) ? strtoul(argv[3], NULL, 0) : 1048575;

  shell_print(sh, "Setting PLLA to %u + %u/%u...", mult, num, denom);
  gSi5351.setPLLA(mult, num, denom);
  shell_print(sh, "PLLA set. Check output spectrum and 'si5351 status'.");
  return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(subSi5351,
  SHELL_CMD(init, NULL, "Initialize Si5351 [ref_hz] [base_hz]", cmdInit),
  SHELL_CMD(ref, NULL, "View or set reference clock frequency in Hz: ref [ref_hz]", cmdRef),
  SHELL_CMD(pll, NULL, "Set PLLA multiplier/numerator/denominator: pll <mult> <num> [denom]", cmdPLL),
  SHELL_CMD(freq, NULL, "Set frequency for clock channel: freq <clk_0_to_7> <freq_hz>", cmdFreq),
  SHELL_CMD(offset, NULL, "Tune clock offset in milliHz: offset <clk_0_to_7> <milliHz>", cmdOffset),
  SHELL_CMD(wspr, NULL, "Tune clock offset in milliHz (alias): wspr <clk_0_to_7> <milliHz>", cmdOffset),
  SHELL_CMD(phase, NULL, "Set phase offset units: phase <clk_0_to_7> <units_0_to_127>", cmdPhase),
  SHELL_CMD(enable, NULL, "Enable/disable clock output: enable <clk_0_to_7> <0|1>", cmdEnable),
  SHELL_CMD(drive, NULL, "Set drive strength: drive <clk_0_to_7> <2|4|6|8>", cmdDrive),
  SHELL_CMD(status, NULL, "Print live hardware ground truth status & parameters [clk]", cmdStatus),
  SHELL_CMD(reg_read, NULL, "Read register byte <reg>", cmdRegRead),
  SHELL_CMD(reg_write, NULL, "Write register byte <reg> <val>", cmdRegWrite),
  SHELL_CMD(scan, NULL, "Scan I2C bus for responding hardware addresses", cmdScan),
  SHELL_CMD(dump, NULL, "Dump all 256 registers", cmdDump),
  SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(si5351, &subSi5351, "Si5351 Clock Generator Commands", NULL);

static int cmdDdsInit(const struct shell *sh, size_t argc, char **argv) {
  uint32_t refHz = 125000000;
  if (argc > 1) {
    refHz = strtoul(argv[1], NULL, 0);
  }
  shell_print(sh, "Initializing AD9850 DDS (Default Ref Clock: %.3f MHz)...",
              static_cast<double>(refHz) / 1000000.0);
  
  gDDS.setPhaseAndPower(0, false);
  gDDS.setFreqCustomRef(10000000, refHz);

  if (gDDS.init()) {
    shell_print(sh, "AD9850 initialization SUCCESS.");
  } else {
    shell_error(sh, "AD9850 initialization FAILED (Check GPIO configuration).");
    return -EIO;
  }
  return 0;
}

static int cmdDdsFreq(const struct shell *sh, size_t argc, char **argv) {
  if (argc < 2) {
    shell_error(sh, "Usage: dds freq <freq_hz> [ref_hz]");
    return -EINVAL;
  }
  uint32_t freqHz = strtoul(argv[1], NULL, 0);
  uint32_t refHz = gDDS.getRefClockHz();
  if (argc > 2) {
    refHz = strtoul(argv[2], NULL, 0);
  }

  shell_print(sh, "Setting AD9850 frequency to %u Hz (%.6f MHz)...",
              freqHz, static_cast<double>(freqHz) / 1000000.0);
  gDDS.setFreqCustomRef(freqHz, refHz);
  return 0;
}

static int cmdDdsPhase(const struct shell *sh, size_t argc, char **argv) {
  if (argc < 2) {
    shell_error(sh, "Usage: dds phase <0..31>");
    return -EINVAL;
  }
  uint8_t phaseVal = static_cast<uint8_t>(strtoul(argv[1], NULL, 0));
  if (phaseVal > 31) {
    shell_error(sh, "Invalid phase value (must be 0 to 31).");
    return -EINVAL;
  }
  shell_print(sh, "Setting AD9850 phase to %u (%.2f degrees)...",
              phaseVal, static_cast<double>(phaseVal) * 11.25);
  gDDS.setPhaseAndPower(phaseVal, gDDS.isPowerDown());
  return 0;
}

static int cmdDdsPower(const struct shell *sh, size_t argc, char **argv) {
  if (argc < 2) {
    shell_error(sh, "Usage: dds power <0|1>");
    return -EINVAL;
  }
  bool pd = (strtoul(argv[1], NULL, 0) != 0);
  shell_print(sh, "Setting AD9850 power-down state to: %s", pd ? "POWER-DOWN" : "ACTIVE");
  gDDS.setPhaseAndPower(gDDS.getPhase(), pd);
  return 0;
}

static int cmdDdsStatus(const struct shell *sh, size_t argc, char **argv) {
  shell_print(sh, "--- AD9850 DDS Live Status ---");
  shell_print(sh, "  Output Freq: %.6f MHz (%u Hz)",
              static_cast<double>(gDDS.getFreqHz()) / 1000000.0, gDDS.getFreqHz());
  shell_print(sh, "  Ref Clock:   %.3f MHz (%u Hz)",
              static_cast<double>(gDDS.getRefClockHz()) / 1000000.0, gDDS.getRefClockHz());
  shell_print(sh, "  Phase Value: %u (%.2f degrees)",
              gDDS.getPhase(), static_cast<double>(gDDS.getPhase()) * 11.25);
  shell_print(sh, "  Power State: %s", gDDS.isPowerDown() ? "POWER-DOWN" : "ACTIVE");
  return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(subDDS,
  SHELL_CMD(init, NULL, "Initialize AD9850 [ref_clock_hz]", cmdDdsInit),
  SHELL_CMD(freq, NULL, "Set frequency: freq <freq_hz> [ref_clock_hz]", cmdDdsFreq),
  SHELL_CMD(phase, NULL, "Set phase value: phase <0..31>", cmdDdsPhase),
  SHELL_CMD(power, NULL, "Set power down state: power <0|1>", cmdDdsPower),
  SHELL_CMD(status, NULL, "Print live AD9850 status", cmdDdsStatus),
  SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(dds, &subDDS, "AD9850 DDS Controller Commands", NULL);

void initShellSi5351() {
}
