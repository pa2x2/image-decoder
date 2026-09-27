#include "box_downsampler.h"
#include "support/decode_harness.h"

#include <vector>

namespace {

std::vector<uint8_t> downsample(const std::vector<uint8_t>& pixels,
                                uint32_t sampleSize, uint32_t components,
                                bool lastComponentIsAlpha) {
  BoxDownsampler downsampler(1, sampleSize, components, lastComponentIsAlpha);
  const size_t rowBytes = static_cast<size_t>(sampleSize) * components;
  for (uint32_t row = 0; row < sampleSize; ++row) {
    downsampler.addRow(pixels.data() + row * rowBytes);
  }
  std::vector<uint8_t> output(components);
  downsampler.writeRow(output.data());
  return output;
}

void require_every_block_pixel_contributes() {
  // One white pixel in the corner of a 4x4 black block is 1/16 of it. Sampling
  // only the middle of the block would lose it entirely.
  std::vector<uint8_t> block(4 * 4, 0);
  block[0] = 255;
  require_condition(downsample(block, 4, 1, false) == std::vector<uint8_t>{16},
                    "box downsampling must average the whole block");
}

void require_transparent_pixels_do_not_tint_the_block() {
  const std::vector<uint8_t> block = {255, 0, 0, 255, 0, 0, 255, 0,
                                      0,   0, 255, 0, 0, 0, 255, 0};
  require_condition(downsample(block, 2, 4, true) ==
                        std::vector<uint8_t>{255, 0, 0, 64},
                    "box downsampling must weight colors by alpha");
}

void require_components_without_alpha_average_evenly() {
  // CMYK has four components, none of which is alpha.
  const std::vector<uint8_t> block = {100, 0, 0, 0,   200, 0, 0, 255,
                                      100, 0, 0, 0,   200, 0, 0, 255};
  require_condition(downsample(block, 2, 4, false) ==
                        std::vector<uint8_t>{150, 0, 0, 128},
                    "box downsampling must not weight by a non-alpha component");
}

} // namespace

void run_box_downsampler_tests() {
  require_every_block_pixel_contributes();
  require_transparent_pixels_do_not_tint_the_block();
  require_components_without_alpha_average_evenly();
}
