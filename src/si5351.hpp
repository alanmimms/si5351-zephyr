// SI5351 driver
//
// This driver assumes that clk0 and clk1 are outputs from the same
// frequency with clk0 at 0 degrees phase offset and clk1 at 180
// degrees phase offset - a push-pull clock. This assumption is
// convenient allowing these two clocks to be driven from the same
// SI5351 internal PLL.

// In general in this driver, if an API sets a thing's value it
// provides the thing's previous value as its return value.

// Several good ideas are taken from Pavel Milanes <pavelmc@gmail.com>
// to whom I am grateful for his diligence and experimental spirit.

/****************************************************************************
 *  This lib tricks:
 *
 * CLK0 will use PLLA
 * CLK1 will use PLLA
 * CLK2 will use PLLA
 *
 * Lib defaults
 * - XTAL is 26 Mhz.
 * - Always put the internal 8pF across the xtal legs to GND
 * - lowest power output (2mA)
 * - After the init all outputs are off, you need to enable them in your code.
 *
 * The correction procedure is not for the PPM as other libs, this
 * is just +/- Hz to the XTAL freq, you may get a click noise after
 * applying a correction
 *
 * The init procedure is mandatory as it set the Xtal (the default or a custom
 * one) and prepare the Wire (I2C) library for operation.
 ****************************************************************************/

#ifndef __SI5351_H__
#define __SI5351_H__

#include <stdint.h>

// default I2C address of the Si5351A - other variants may differ
#define SIADDR 0x60

// The number of output channels - 3 for Si5351A 10 pin
#define SICHANNELS 3

// register's power modifiers
#define SIOUT_2mA 0
#define SIOUT_4mA 1
#define SIOUT_6mA 2
#define SIOUT_8mA 3


// registers base (2mA by default)
#define SICLK0_R   76       // 0b01001100
#define SICLK12_R 108       // 0b01101100

class SI5351 {
public:
  static constexpr unsigned I2CAddress = 0x60; 	// I2C address of our target
  static constexpr int nChannels = 3;		// Number of channels in SI5351A

  enum DriveStrength {ds2mA, s4mA, ds6mA, ds8mA};
  enum PLLSource {pllSrcXTAL, pllSrcCLKIN};
  enum CLKINDivider {div1, div2, div4, div8};

  enum InterruptMask {
    intSysInit = 0x80,
    intLOL_B = 0x40,
    intLOL_A = 0x20,
    intLOS_CLKIN = 0x10,
    intLOS_XTAL = 0x08,
    intNO_CLEAR = 0xFF,
  };

private:
  DriveStrength driveStrength[nChannels];

  uint32_t clockHz;		// Nominal frequency of the CLKIN signal oscillator
  int32_t correctionHz = 0;	// Correction factor we apply to compensate for oscillator offset

  enum RegisterNumber {
    deviceStatus = 0,
    intStatusSticky = 1,
    intStatusMask = 2,
    outputDisable = 3,
    OEBPinDisable = 9,
    PLLInputSource = 15,
    clkControlBase = 16,
    clk0_3Disable = 24,
    clk4_7Disable = 25,
    msNABase = 26,		// 26..33
    msNBBase = 34,		// 34..41
    ms0Base = 42,		// 42..49
    ms1Base = 50,		// 50..57
    ms2Base = 58,		// 58..65
    ms3Base = 66,		// 66..73
    ms4Base = 74,		// 74..81
    ms5Base = 82,		// 82..89
    ms6 = 90,
    ms7 = 91,
    clock6_7Divider = 92,
    ssEnable = 149,
    VCXOParameterBase = 162,	// 162..164
    clkPhaseOffsetBase = 165,	// 0..5
    PLLReset = 177,
    crystalLoadCapacitance = 183,
    fanoutEnable = 187,
  };


  // local var to keep track of when to reset the "pll"
  /*********************************************************
   * BAD CONCEPT on the datasheet and AN:
   *
   * The chip has a soft-reset for PLL A & B but in
   * practice the PLL does not need to be reseted.
   *
   * Test shows that if you fix the Msynth output
   * dividers and move any of the VCO from bottom to top
   * the frequency moves smooth and clean, no reset needed
   *
   * The reset is needed when you changes the value of the
   * Msynth output divider, even so it's not always needed
   * so we use this var to keep track of all three and only
   * reset the "PLL" when this value changes to be sure
   *
   * It's a word (16 bit) because the final max value is 900
   *********************************************************/
  uint16_t  omsynth[SICHANNELS] = { 0 };
  uint8_t   o_Rdiv[SICHANNELS] = { 0 };

public:
  // var to check the clock state
  bool clkOn[SICHANNELS] = { 0 };     // This should not really be public - use isEnabled()

public:
  SI5351A(uint32_t clkinHz = 26*1000*1000);

  void reset();

  void setPLLASource(PLLSource src);
  void setPLLBSource(PLLSource src);
  void setCLKINDivider(CLKINDivider div);

  InterruptMask getIntStatus();
  InterruptMask getSetIntStatusSticky(InterruptMask write0ToClear);

  uint8_t getCLKsDisabled(uint8_t clkMask);
  uint8_t setCLKsDisabled(uint8_t clkMask);


  // default init procedure
  void init(void);

  // custom init procedure (XTAL in Hz);
  void init(uint32_t refHz);

  // reset all PLLs
  static void reset(void);

  // set CLKx(0..2) to freq (Hz)
  void setFreq(uint8_t clkNum, uint32_t freqHz);

  // pass a correction factor (signed offset in Hz).
  void correction(int32_t realRefOffsetFromPresumedHz);

  // enable some CLKx output
  void enable(uint8_t clkNum);

  // disable some CLKx output
  void disable(uint8_t clkNum);

  // disable all outputs
  void off(void);

  // set power output to a specific clk
  void setPower(uint8_t clkNum, uint8_t driveStrengthInmA);

  // used to talk with the chip, via Arduino Wire lib
  //
  // declared as static, since they do not reference any this-> class attributes
  //
  static void     i2cWrite( const uint8_t reg, const uint8_t val );
  static uint8_t  i2cWriteBurst( const uint8_t start_register, const uint8_t *data, const uint8_t numbytes );
  static int16_t  i2cRead( const uint8_t reg );
        
  inline const bool isEnabled( const uint8_t channel ) {
    return channel < SICHANNELS && clkOn[ channel ] != 0;
  };
        
  inline const uint8_t getPower( const uint8_t channel ) {
    return channel < SICHANNELS ? clkpower[ channel ] : 0;  
  };

  inline const uint32_t getXtalBase( void ) {
    return base_xtal;
  };

  inline const uint32_t getXtalCurrent( void ) {
    return int_xtal;
  };
        
};


#endif //__SI5351_H__
