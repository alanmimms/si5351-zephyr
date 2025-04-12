#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>

// I2C bus address of the SI5351A
enum {
  SI5351_I2C_ADDRESS = 0x60,
};

// I2C addresses within the SI5351A.
enum {
  SI5351_deviceStatus = 0,
};


static const struct device *i2cDev = DEVICE_DT_GET(DT_NODELABEL(i2c1));


// NOTE: I2C is not thread safe.
int main(void) {
  int st;

  if (i2cDev == NULL || !device_is_ready(i2cDev)) {
    printf("Could not get I2C device\n");
    return -1;
  }

  uint8_t devStatus = 0;
  st = i2c_reg_read_byte(i2cDev, SI5351_I2C_ADDRESS, SI5351_deviceStatus, &devStatus);

  if (st) {
    printf("Unable to get SI5351A device status register (0) (err %i)\n", st);
    return -1;
  }

  printf("SI5351A device_status=%02X\n", (int) devStatus);

  uint8_t regs[256];
  
  st = i2c_burst_read(i2cDev, SI5351_I2C_ADDRESS, 0, regs, sizeof(regs));

  if (st) {
    printf("Unable to burst read all SI5351A registers (err %i)\n", st);
    return -1;
  }

  printf("\nSI5351 Registers:");
  for (int k=0; k < 256; ++k) {
    if (k % 16 == 0) printf("\n%02X: ", k);
    printf("%02X ", regs[k]);
  }

  printf("\n");
  return 0;
}
