#include "JTEncodeUtil.hpp"
#include "JTEncode.hpp"
#include "RSEncode.hpp"
#include <iostream>
#include <string>
#include <vector>
#include <iomanip>

void runJtCodeTest(const std::string& testName, char inputChar, uint8_t expectedOutput) {
  uint8_t actualOutput = jtCode(inputChar);
  std::cout << "  Test: " << testName << " ('" << inputChar << "') -> Expected: "
            << static_cast<int>(expectedOutput) << ", Actual: "
            << static_cast<int>(actualOutput);

  if (actualOutput == expectedOutput) {
    std::cout << " [PASS]" << std::endl;
  } else {
    std::cout << " [FAIL]" << std::endl;
  }
}

void printVector(const std::string& label, const std::vector<uint8_t>& vec) {
  std::cout << label << " (hex): ";
  if (vec.empty()) {
    std::cout << "[EMPTY]";
  } else {
    for (uint8_t b : vec) {
      std::cout << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b) << " ";
    }
  }
  std::cout << std::endl;
}

int main() {
  std::cout << "Starting JTEncode Component Tests..." << std::endl;

  std::cout << "\n--- Section 1: jtCode Utility Tests ---" << std::endl;

  std::cout << "\n--- Test Case 1.1: Digits ---" << std::endl;
  for (char c = '0'; c <= '9'; ++c) {
    runJtCodeTest("Digit " + std::string(1, c), c, static_cast<uint8_t>(c - '0'));
  }

  std::cout << "\n--- Test Case 1.2: Uppercase Letters ---" << std::endl;
  for (char c = 'A'; c <= 'Z'; ++c) {
    runJtCodeTest("Uppercase " + std::string(1, c), c, static_cast<uint8_t>(c - 'A' + 10));
  }

  std::cout << "\n--- Test Case 1.3: Specific Special Characters ---" << std::endl;
  runJtCodeTest("Space", ' ', 36);
  runJtCodeTest("Plus", '+', 37);
  runJtCodeTest("Minus", '-', 38);
  runJtCodeTest("Period", '.', 39);
  runJtCodeTest("Slash", '/', 40);
  runJtCodeTest("Question Mark", '?', 41);

  std::cout << "\n--- Test Case 1.4: Invalid/Unhandled Characters ---" << std::endl;
  runJtCodeTest("Lowercase 'a'", 'a', 255);
  runJtCodeTest("Lowercase 'z'", 'z', 255);
  runJtCodeTest("Exclamation Mark '!'", '!', 255);

  std::cout << "\n--- Section 2: WSPREncoder Integration Test ---" << std::endl;
  try {
    WSPREncoder wsprEncoder(14097000UL + 1500);
    wsprEncoder.encode("K1JT", "FN20", 20);

    std::cout << "  WSPR encoding executed successfully for K1JT FN20 20 dBm." << std::endl;
    std::cout << "  Generated " << WSPREncoder::txBufferSize << " channel symbols." << std::endl;
    std::cout << "  First 8 symbols: ";
    for (int i = 0; i < 8; ++i) {
      std::cout << static_cast<int>(wsprEncoder.symbols[i]) << " ";
    }
    std::cout << std::endl;

  } catch (const std::exception& e) {
    std::cerr << "WSPREncoder test failed: " << e.what() << std::endl;
    return 1;
  }

  std::cout << "\nAll JTEncode Component Tests completed." << std::endl;
  return 0;
}
