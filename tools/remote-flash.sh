#!/bin/bash
# remote-flash.sh - Executed by west build to flash on nano1 via SSH
# $1 is the binary path relative to project root (e.g. zephyr/zephyr.bin)

TARGET_IP="nano1.local"
TARGET_PORT="/dev/ttyACM0"
TARGET_BIN="/homealan/ham/si5351-zephyr/build/$1"
REMOTE_ESPTOOL="/home/alan/.venv/bin/esptool"

echo "--- Redirecting Flash to ${TARGET_IP} (${TARGET_PORT}) ---"

ssh ${TARGET_IP} "fuser -k ${TARGET_PORT} 2>/dev/null || true; sleep 0.5; \
  ${REMOTE_ESPTOOL} --chip esp32c3 \
  --port ${TARGET_PORT} \
  --baud 921600 \
  write-flash 0x0 ${TARGET_BIN}"

echo "--- Remote Flash Finished Successfully ---"
