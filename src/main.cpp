#include <zephyr/kernel.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>
#include "Si5351.hpp"
#include "AD9850.hpp"
#include "shell-commands.hpp"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

Si5351 gSi5351;
const struct i2c_dt_spec gI2CSpec = I2C_DT_SPEC_GET(DT_NODELABEL(si5351a));

int main(void) {
  LOG_INF("Starting Si5351 & AD9850 C++20 Zephyr Shell Testbed...");

  // Auto-initialize AD9850 DDS (harmless if not present)
  gDDS.init();

  if (!i2c_is_ready_dt(&gI2CSpec)) {
    LOG_ERR("I2C device %s is NOT ready! Check hardware wiring.", gI2CSpec.bus->name);
  } else {
    LOG_INF("I2C bus %s ready. Initializing Si5351...", gI2CSpec.bus->name);
    // Auto-initialize with calibrated 25.997050 MHz TCXO reference clock and 14.0956 MHz (20m WSPR band base)
    if (!gSi5351.init(&gI2CSpec, 25997050, 14095600)) {
      LOG_WRN("Si5351 auto-init failed. You can use 'si5351 init' in the shell to retry.");
    }
  }

  LOG_INF("Zephyr shell active. Type 'si5351 help' or 'dds help' for commands.");
  return 0;
}
