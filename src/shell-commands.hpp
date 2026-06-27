#pragma once
#include "Si5351.hpp"
#include <zephyr/drivers/i2c.h>

extern Si5351 gSi5351;
extern const struct i2c_dt_spec gI2CSpec;

void initShellSi5351();
