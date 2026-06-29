#pragma once

#include <cstdint>
#include <vector>
#include <span>
#include <string>
#include <stdexcept>
#include <cctype>
#include <utility>

/**
 * @class RSEncode
 * @brief A C++20 class for Reed-Solomon encoding.
 */
class RSEncode {
public:
  RSEncode(int symsize, int gfpoly, int fcr, int prim, int nroots, int pad);
  ~RSEncode();

  RSEncode(const RSEncode&) = delete;
  RSEncode& operator=(const RSEncode&) = delete;

  RSEncode(RSEncode&& other) noexcept;
  RSEncode& operator=(RSEncode&& other) noexcept;

  void encode(std::span<const uint8_t> data, std::span<uint8_t> parity) const;
  void encode(const std::vector<uint8_t>& data, std::vector<uint8_t>& parity) const;

  constexpr int getNN() const { return nn; }
  constexpr int getNRoots() const { return nroots; }
  constexpr int getPad() const { return pad; }

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

  constexpr int modNNPrivate(int x) const;
};
