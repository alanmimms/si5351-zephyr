# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# Set default board for west build when not specified on command line
set(BOARD esp32c3_devkitm CACHE STRING "Board target")

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(si5351)

target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_sources(app PRIVATE
  src/Si5351.cpp
  src/main.cpp
  src/shell-commands.cpp
)

# Custom target for flashing remote target machine over SSH
set(toolsDirPath "${CMAKE_CURRENT_SOURCE_DIR}/tools")
set(flashScriptPath "${toolsDirPath}/remote-flash.sh")

add_custom_target(remoteFlash
  COMMAND bash ${flashScriptPath} zephyr/zephyr.bin
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  DEPENDS zephyr_final
)
