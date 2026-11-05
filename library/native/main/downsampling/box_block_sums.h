#ifndef IMAGEDECODER_BOX_BLOCK_SUMS_H
#define IMAGEDECODER_BOX_BLOCK_SUMS_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

// Sums the interleaved 8-bit pixels of each of a number of sampleSize x
// sampleSize blocks and resolves every block to the average of its pixels.
// When the last component is alpha, colors are weighted by it so transparent
// pixels do not tint their block. `Sum` only has to hold the total of one
// block, so callers that keep many blocks at once can use a narrower type.
template <typename Sum> class BoxBlockSums {
public:
  // Whether `Sum` can hold the total of a whole block.
  static bool holdsBlocksOf(uint32_t sampleSize, bool lastComponentIsAlpha) {
    const uint64_t largestPixelSum = lastComponentIsAlpha ? 255 * 255 : 255;
    const uint64_t pixelCount = static_cast<uint64_t>(sampleSize) * sampleSize;
    return pixelCount <= std::numeric_limits<Sum>::max() / largestPixelSum;
  }

  BoxBlockSums(size_t blockCount, uint32_t sampleSize, uint32_t components,
               bool lastComponentIsAlpha)
      : components(components),
        colorComponents(lastComponentIsAlpha ? components - 1 : components),
        lastComponentIsAlpha(lastComponentIsAlpha),
        pixelCount(static_cast<uint64_t>(sampleSize) * sampleSize),
        sums(blockCount * components) {
    if (!holdsBlocksOf(sampleSize, lastComponentIsAlpha)) {
      throw std::overflow_error("Downsampled block is too large to sum");
    }
  }

  void add(size_t block, const uint8_t* pixel) {
    Sum* blockSums = sums.data() + block * components;
    const Sum weight = lastComponentIsAlpha ? pixel[colorComponents] : 1;
    for (uint32_t component = 0; component < colorComponents; ++component) {
      blockSums[component] += weight * pixel[component];
    }
    if (lastComponentIsAlpha) {
      blockSums[colorComponents] += weight;
    }
  }

  // Writes the average of a block whose every pixel has been added.
  void resolve(size_t block, uint8_t* output) const {
    const Sum* blockSums = sums.data() + block * components;
    const uint64_t colorWeight =
        lastComponentIsAlpha ? blockSums[colorComponents] : pixelCount;
    for (uint32_t component = 0; component < colorComponents; ++component) {
      output[component] =
          colorWeight == 0
              ? 0
              : static_cast<uint8_t>((blockSums[component] + colorWeight / 2) /
                                     colorWeight);
    }
    if (lastComponentIsAlpha) {
      output[colorComponents] = static_cast<uint8_t>(
          (blockSums[colorComponents] + pixelCount / 2) / pixelCount);
    }
  }

  void clear() { std::fill(sums.begin(), sums.end(), 0); }

private:
  uint32_t components;
  uint32_t colorComponents;
  bool lastComponentIsAlpha;
  uint64_t pixelCount;
  std::vector<Sum> sums;
};

#endif // IMAGEDECODER_BOX_BLOCK_SUMS_H
