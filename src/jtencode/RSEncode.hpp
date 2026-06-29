#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <stdexcept>
#include <cctype>
#include <utility>

/**
 * @class RSEncode
 * @brief A C++ class for Reed-Solomon encoding.
 */
class RSEncode {
public:
  RSEncode(int symsize, int gfpoly, int fcr, int prim, int nroots, int pad);
  ~RSEncode();

  RSEncode(const RSEncode&) = delete;
  RSEncode& operator=(const RSEncode&) = delete;

  RSEncode(RSEncode&& other) noexcept;
  RSEncode& operator=(RSEncode&& other) noexcept;

  void encode(const std::vector<uint8_t>& data, std::vector<uint8_t>& parity) const;

  int getNN() const { return nn; }
  int getNRoots() const { return nroots; }
  int getPad() const { return pad; }

private:
  int mm;
  int nn;
  int nroots;
  int fcr;
  int prim;
  int iprim;
  int pad;

  std::vector<uint8_t> alphaTo;
  std::vector<uint8_t> indexOf;
  std::vector<uint8_t> genPoly;

  int modNNPrivate(int x) const;
};
