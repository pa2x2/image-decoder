#include "downsampling/box_block_sums.h"
#include "support/decode_harness.h"

#include <array>
#include <stdexcept>
#include <vector>

namespace {

std::vector<uint8_t> average(const std::vector<uint8_t>& pixels,
                             uint32_t sampleSize, uint32_t components,
                             bool lastComponentIsAlpha) {
  BoxBlockSums<uint64_t> sums(1, sampleSize, components, lastComponentIsAlpha);
  for (size_t offset = 0; offset < pixels.size(); offset += components) {
    sums.add(0, pixels.data() + offset);
  }
  std::vector<uint8_t> output(components);
  sums.resolve(0, output.data());
  return output;
}

void require_transparent_pixels_do_not_tint_the_block() {
  const std::vector<uint8_t> block = {255, 0, 0,   255, 0, 0, 255, 0,
                                      0,   0, 255, 0,   0, 0, 255, 0};
  require_condition(average(block, 2, 4, true) ==
                        std::vector<uint8_t>{255, 0, 0, 64},
                    "box averaging must weight colors by alpha");
}

void require_fully_transparent_block_is_transparent_black() {
  const std::vector<uint8_t> block = {255, 128, 64, 0, 10, 20, 30, 0,
                                      40,  50,  60, 0, 70, 80, 90, 0};
  require_condition(average(block, 2, 4, true) ==
                        std::vector<uint8_t>{0, 0, 0, 0},
                    "a block without any opaque weight must have no color");
}

void require_components_without_alpha_average_evenly() {
  // CMYK has four components, none of which is alpha.
  const std::vector<uint8_t> block = {100, 0, 0, 0, 200, 0, 0, 255,
                                      100, 0, 0, 0, 200, 0, 0, 255};
  require_condition(average(block, 2, 4, false) ==
                        std::vector<uint8_t>{150, 0, 0, 128},
                    "box averaging must not weight by a non-alpha component");
}

void require_narrow_sums_hold_every_block_they_accept() {
  uint32_t largest = 1;
  while (BoxBlockSums<uint32_t>::holdsBlocksOf(largest + 1, true)) {
    ++largest;
  }
  // Keeps the sums of every block of a whole sampled image narrow for the
  // power-of-two sample sizes that are used in practice.
  require_condition(largest >= 256,
                    "32-bit sums must hold blocks of sample size 256");

  const std::array<uint8_t, 4> opaqueWhite = {255, 255, 255, 255};
  BoxBlockSums<uint32_t> sums(1, largest, 4, true);
  for (uint64_t pixel = 0; pixel < static_cast<uint64_t>(largest) * largest;
       ++pixel) {
    sums.add(0, opaqueWhite.data());
  }
  std::array<uint8_t, 4> output{};
  sums.resolve(0, output.data());
  require_condition(output == opaqueWhite,
                    "32-bit sums must not overflow on the largest block");

  bool rejected = false;
  try {
    BoxBlockSums<uint32_t>(1, largest + 1, 4, true);
  } catch (const std::overflow_error&) {
    rejected = true;
  }
  require_condition(rejected,
                    "32-bit sums must reject blocks they would overflow on");
}

} // namespace

void run_box_block_sums_tests() {
  require_transparent_pixels_do_not_tint_the_block();
  require_fully_transparent_block_is_transparent_black();
  require_components_without_alpha_average_evenly();
  require_narrow_sums_hold_every_block_they_accept();
}
