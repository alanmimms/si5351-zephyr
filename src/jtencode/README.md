# JTEncode Baseband Encoder Library

`jtencode` is a modernized C++17 baseband digital modulation encoder
library designed for amateur radio modes (such as WSPR and JT modes)
operating within Zephyr RTOS and host test environments.

Originally derived from the Etherkit JTEncode project and adapted away
from ESP32-IDF / Arduino component wrappers, this subtree provides a
clean, self-contained C++ API with a standalone host CMake build and
unit test suite.

---

## Architecture & Classes

### 1. `JTEncoder` & `WSPREncoder` (`JTEncode.hpp` / `JTEncode.cpp`)

* **`JTEncoder`**: Base template class defining fundamental modulation
  parameters (`toneSpacing`, `symbolPeriod`, `txBufferSize`) and
  providing virtual encoding pipeline hooks.

* **`WSPREncoder`**: Active WSPR transmission packet generator.

  * **Bit Packing**: Encodes callsign, 4-digit grid locator, and
    output power (dBm) into a 50-bit payload.

  * **Convolutional Encoding**: Applies rate-1/2 constraint-length 32
    convolutional encoding (using polynomials `0xF2D05351` and
    `0xE4613C47`) to generate 162 symbol pairs.

  * **Interleaving**: Reorders symbol sequence using WSPR bit-reversal
    hashing to protect against RF burst errors.

### 2. `RSEncode` (`RSEncode.hpp` / `RSEncode.cpp`)
* An object-oriented C++ Reed-Solomon Forward Error Correction (FEC)
  engine.
* Manages Galois Field GF($2^m$) lookup tables (`alphaTo`, `indexOf`)
  dynamically using `std::vector` and RAII principles. Supports
  shortened blocks and arbitrary generator polynomials with full
  constructor parameter validation.

### 3. `JTEncodeUtil` (`JTEncodeUtil.hpp` / `JTEncodeUtil.cpp`)
* Provides character-to-symbol mapping (`jtCode(char c)`) for valid
  WSPR/JT alphanumeric and symbol sets (digits `0`-`9`, uppercase
  `A`-`Z`, spaces, and punctuation).

---

## Host Build and Unit Testing

The `jtencode` library is configured for host machine compilation and
automated testing using standard CMake and CTest.

### Building and Running Tests

From the repository root:

```bash
# Create and navigate to the build directory
mkdir -p src/jtencode/build
cd src/jtencode/build

# Configure CMake and build static library and test targets
cmake ..
cmake --build .

# Execute unit tests with detailed output
ctest --verbose
```

### Test Suite Overview
* **`test-rsencode`**: Verifies Reed-Solomon Galois Field math,
  RS(7,3) and RS(15,8) parity generation, block padding, data
  truncation safety, and invalid parameter exception handling.
* **`test-jtencode`**: Verifies `jtCode()` character translation
  matrices and executes an end-to-end `WSPREncoder` test generating a
  full 162-symbol channel packet.
