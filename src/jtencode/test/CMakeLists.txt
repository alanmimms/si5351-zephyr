cmake_minimum_required(VERSION 3.16)

add_executable(test-rsencode rsencode-test-main.cpp)
target_link_libraries(test-rsencode PRIVATE jtencode)

add_executable(test-jtencode jtencode-test-main.cpp)
target_link_libraries(test-jtencode PRIVATE jtencode)

add_test(NAME rsencode COMMAND test-rsencode)
add_test(NAME jtencode COMMAND test-jtencode)
