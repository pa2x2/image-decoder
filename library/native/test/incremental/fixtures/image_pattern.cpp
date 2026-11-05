#include "fixtures/image_pattern.h"

#include <cstddef>

std::vector<uint8_t> make_fixture_pattern(bool alpha) {
  std::vector<uint8_t> rgba(static_cast<size_t>(kFixtureWidth) *
                            kFixtureHeight * 4);
  for (uint32_t y = 0; y < kFixtureHeight; ++y) {
    for (uint32_t x = 0; x < kFixtureWidth; ++x) {
      const size_t offset = (static_cast<size_t>(y) * kFixtureWidth + x) * 4;
      rgba[offset] =
          static_cast<uint8_t>((x * 13 + y * 7 + (x * y) % 251) & 0xFF);
      rgba[offset + 1] =
          static_cast<uint8_t>((x * 3 + y * 17 + (x ^ y) * 5) & 0xFF);
      rgba[offset + 2] =
          static_cast<uint8_t>((x * 19 + y * 11 + (x * 5 ^ y * 9)) & 0xFF);
      rgba[offset + 3] =
          alpha ? static_cast<uint8_t>(64 + (x * 11 + y * 17) % 192) : 255;
    }
  }
  return rgba;
}
