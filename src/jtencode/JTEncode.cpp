#include "JTEncode.hpp"
#include "JTEncodeUtil.hpp"
#include <cstring>
#include <cstdio>

static uint32_t nhash(const int* d) {
  uint32_t h = 0x811c9dc5;
  for (int i = 0; i < 2; ++i) {
    h ^= static_cast<uint32_t>(d[i]);
    h *= 0x01000193;
  }
  return h;
}

void WSPREncoder::encode(const char* callsign, const char* locator, int8_t powerDbm) {
  std::strncpy(this->callsign, callsign, 11);
  this->callsign[11] = '\0';
  std::strncpy(this->locator, locator, 6);
  this->locator[6] = '\0';
  this->powerDbm = powerDbm;
  packBits();
  convolveSymbols();
  interleave();
}

void WSPREncoder::packBits() {
  std::memset(packedData, 0, sizeof(packedData));
  uint64_t n = 0;
  uint32_t nCall = 0;
  nCall += jtCode(callsign[0]) * 36 * 36 * 36 * 36 * 10;
  nCall += jtCode(callsign[1]) * 36 * 36 * 36 * 10;
  nCall += jtCode(callsign[2]) * 36 * 36 * 10;
  nCall += jtCode(callsign[3]) * 36 * 10;
  nCall += jtCode(callsign[4]) * 10;
  nCall += jtCode(callsign[5]);
  n = nCall;
  uint16_t nLoc = 179 - (locator[0] - 'A') * 10 - (locator[1] - 'A');
  nLoc *= 100;
  nLoc += (locator[2] - '0') * 10 + (locator[3] - '0');
  n = (n << 15) | nLoc;
  uint8_t nPow = (powerDbm >= 0 && powerDbm <= 60) ? static_cast<uint8_t>(powerDbm) : 63;
  n = (n << 7) | nPow;
  for (int i = 0; i < 50; ++i) {
    if ((n >> (49 - i)) & 1) packedData[i / 8] |= (1 << (7 - (i % 8)));
  }
}

void WSPREncoder::convolveSymbols() {
  const uint32_t g1 = 0xF2D05351;
  const uint32_t g2 = 0xE4613C47;
  uint8_t messageBuffer[205] = {0};
  for (int i = 0; i < 50; ++i) {
    messageBuffer[i] = (packedData[i / 8] >> (7 - (i % 8))) & 1;
  }
  for (int i = 0; i < 162; ++i) {
    uint8_t bit1 = 0;
    uint8_t bit2 = 0;
    for (int j = 0; j < 32; ++j) {
      if (((g1 >> j) & 1) != 0) bit1 ^= messageBuffer[i + j];
      if (((g2 >> j) & 1) != 0) bit2 ^= messageBuffer[i + j];
    }
    symbols[i] = (bit1 << 1) | bit2;
  }
}

void WSPREncoder::interleave() {
  uint8_t temporaryBuffer[txBufferSize];
  int d[2] = {0, 0};
  for (int i = 0; i < txBufferSize; ++i) {
    d[0] = i;
    uint32_t h = nhash(d);
    temporaryBuffer[i] = symbols[h % 162];
  }
  std::memcpy(symbols, temporaryBuffer, txBufferSize);
}
