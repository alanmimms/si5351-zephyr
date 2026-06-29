#include "JTEncodeUtil.hpp"

uint8_t jtCode(char c) {
  if (std::isdigit(static_cast<unsigned char>(c))) {
    return static_cast<uint8_t>(c - '0');
  } else if (c >= 'A' && c <= 'Z') {
    return static_cast<uint8_t>(c - 'A' + 10);
  } else {
    switch (c) {
      case ' ': return 36;
      case '+': return 37;
      case '-': return 38;
      case '.': return 39;
      case '/': return 40;
      case '?': return 41;
      default:  return 255;
    }
  }
}
