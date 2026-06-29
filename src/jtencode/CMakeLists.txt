cmake_minimum_required(VERSION 3.16)
project(jtencode CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(jtencode STATIC
    JTEncode.cpp
    RSEncode.cpp
    JTEncodeUtil.cpp
)

target_include_directories(jtencode PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

enable_testing()
add_subdirectory(test)