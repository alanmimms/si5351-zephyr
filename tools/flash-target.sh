#!/bin/bash
esptool.py --chip esp32s3 --port /dev/ttyACM0 --baud 921600 --after hard_reset write_flash 0x0 build/zephyr/zephyr.bin
