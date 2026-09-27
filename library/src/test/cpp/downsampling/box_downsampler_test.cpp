#include "downsampling/box_downsampler.h"
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

} // namespace

void run_box_downsampler_tests() { require_every_block_pixel_contributes(); }
