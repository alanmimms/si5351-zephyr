#include "RSEncode.hpp"
#include <cstring>
#include <stdexcept>
#include <algorithm>

#define RS_A_0_VAL(nnVal) (nnVal)

constexpr int RSEncode::modNNPrivate(int x) const {
  while (x >= nn) {
    x -= nn;
    x = (x >> mm) + (x & nn);
  }
  return x;
}

RSEncode::RSEncode(int symsize, int gfpoly, int fcrParam, int primParam, int nrootsParam, int padParam)
  : mm(symsize), nn((1 << symsize) - 1), nroots(nrootsParam), fcr(fcrParam), prim(primParam), iprim(0), pad(padParam) {

  if (mm < 0 || mm > 8 * static_cast<int>(sizeof(uint8_t))) {
    throw std::runtime_error("RSEncode: Invalid symsize (bits per symbol).");
  }
  if (fcr < 0 || fcr >= (1 << mm)) {
    throw std::runtime_error("RSEncode: Invalid fcr (first consecutive root).");
  }
  if (prim <= 0 || prim >= (1 << mm)) {
    throw std::runtime_error("RSEncode: Invalid prim (primitive element).");
  }
  if (nroots < 0 || nroots >= (1 << mm)) {
    throw std::runtime_error("RSEncode: Invalid nroots (number of roots/parity symbols).");
  }
  if (pad < 0 || pad >= ((1 << mm) - 1 - nroots)) {
    throw std::runtime_error("RSEncode: Invalid pad (padding bytes).");
  }

  alphaTo.resize(nn + 1);
  indexOf.resize(nn + 1);
  genPoly.resize(nroots + 1);

  indexOf[0] = RS_A_0_VAL(nn);
  alphaTo[RS_A_0_VAL(nn)] = 0;
  int sr = 1;
  for (int i = 0; i < nn; ++i) {
    indexOf[sr] = i;
    alphaTo[i] = sr;
    sr <<= 1;
    if (sr & (1 << mm)) {
      sr ^= gfpoly;
    }
    sr &= nn;
  }

  if (sr != 1) {
    throw std::runtime_error("RSEncode: Field generator polynomial is not primitive!");
  }

  genPoly[0] = 1;
  for (int i = 0, root = fcr * prim; i < nroots; ++i, root += prim) {
    genPoly[i + 1] = 1;
    for (int j = i; j > 0; --j) {
      if (genPoly[j] != 0) {
        genPoly[j] = genPoly[j - 1] ^ alphaTo[modNNPrivate(indexOf[genPoly[j]] + root)];
      } else {
        genPoly[j] = genPoly[j - 1];
      }
    }
    genPoly[0] = alphaTo[modNNPrivate(indexOf[genPoly[0]] + root)];
  }

  for (int i = 0; i <= nroots; ++i) {
    genPoly[i] = indexOf[genPoly[i]];
  }

  int iprimVal = 1;
  for (; (iprimVal % prim) != 0; iprimVal += nn);
  iprim = iprimVal / prim;
}

RSEncode::~RSEncode() = default;

RSEncode::RSEncode(RSEncode&& other) noexcept
  : mm(std::move(other.mm)),
    nn(std::move(other.nn)),
    nroots(std::move(other.nroots)),
    fcr(std::move(other.fcr)),
    prim(std::move(other.prim)),
    iprim(std::move(other.iprim)),
    pad(std::move(other.pad)),
    alphaTo(std::move(other.alphaTo)),
    indexOf(std::move(other.indexOf)),
    genPoly(std::move(other.genPoly)) {
}

RSEncode& RSEncode::operator=(RSEncode&& other) noexcept {
  if (this != &other) {
    mm = std::move(other.mm);
    nn = std::move(other.nn);
    nroots = std::move(other.nroots);
    fcr = std::move(other.fcr);
    prim = std::move(other.prim);
    iprim = std::move(other.iprim);
    pad = std::move(other.pad);
    alphaTo = std::move(other.alphaTo);
    indexOf = std::move(other.indexOf);
    genPoly = std::move(other.genPoly);
  }
  return *this;
}

void RSEncode::encode(std::span<const uint8_t> data, std::span<uint8_t> parity) const {
  if (alphaTo.empty() || indexOf.empty() || genPoly.empty()) {
    throw std::runtime_error("RSEncode: Encoder internal tables not initialized. Cannot encode.");
  }

  if (parity.size() < static_cast<size_t>(nroots)) {
    throw std::runtime_error("RSEncode: Output parity span is too small.");
  }

  std::fill(parity.begin(), parity.begin() + nroots, static_cast<uint8_t>(0));

  const int numDataSymbols = nn - nroots - pad;
  const size_t dataSize = data.size();

  for (int i = 0; i < numDataSymbols; ++i) {
    uint8_t currentDataSymbol = (static_cast<size_t>(i) < dataSize) ? data[i] : 0x00;
    uint8_t feedback = indexOf[currentDataSymbol ^ parity[0]];

    if (feedback != RS_A_0_VAL(nn)) {
      for (int j = 1; j < nroots; ++j) {
        parity[j] ^= alphaTo[modNNPrivate(feedback + genPoly[nroots - j])];
      }
    }

    std::move(parity.begin() + 1, parity.begin() + nroots, parity.begin());

    if (feedback != RS_A_0_VAL(nn)) {
      parity[nroots - 1] = alphaTo[modNNPrivate(feedback + genPoly[0])];
    } else {
      parity[nroots - 1] = 0;
    }
  }
}

void RSEncode::encode(const std::vector<uint8_t>& data, std::vector<uint8_t>& parity) const {
  if (parity.size() != static_cast<size_t>(nroots)) {
    parity.resize(nroots);
  }
  encode(std::span<const uint8_t>(data.data(), data.size()), std::span<uint8_t>(parity.data(), parity.size()));
}
