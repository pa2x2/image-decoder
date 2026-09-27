#include "box_downsampler.h"

#include <algorithm>
#include <stdexcept>

BoxDownsampler::BoxDownsampler(uint32_t outputWidth, uint32_t sampleSize,
                               uint32_t components, bool lastComponentIsAlpha)
    : outputWidth(outputWidth), sampleSize(sampleSize), components(components),
      lastComponentIsAlpha(lastComponentIsAlpha),
      sums(static_cast<size_t>(outputWidth) * components) {}

void BoxDownsampler::addRow(const uint8_t* row) {
  if (addedRows == sampleSize) {
    throw std::logic_error("Downsampled block already has all its rows");
  }
  ++addedRows;
  const uint32_t colorComponents =
      lastComponentIsAlpha ? components - 1 : components;
  uint64_t* blockSums = sums.data();
  for (uint32_t block = 0; block < outputWidth; ++block) {
    for (uint32_t pixel = 0; pixel < sampleSize; ++pixel) {
      const uint64_t weight = lastComponentIsAlpha ? row[colorComponents] : 1;
      for (uint32_t component = 0; component < colorComponents; ++component) {
        blockSums[component] += weight * row[component];
      }
      if (lastComponentIsAlpha) {
        blockSums[colorComponents] += weight;
      }
      row += components;
    }
    blockSums += components;
  }
}

void BoxDownsampler::writeRow(uint8_t* output) {
  if (addedRows != sampleSize) {
    throw std::logic_error("Downsampled block is missing rows");
  }
  const uint64_t pixelCount = static_cast<uint64_t>(sampleSize) * sampleSize;
  const uint32_t colorComponents =
      lastComponentIsAlpha ? components - 1 : components;
  const uint64_t* blockSums = sums.data();
  for (uint32_t block = 0; block < outputWidth; ++block) {
    const uint64_t colorWeight =
        lastComponentIsAlpha ? blockSums[colorComponents] : pixelCount;
    for (uint32_t component = 0; component < colorComponents; ++component) {
      output[component] =
          colorWeight == 0 ? 0
                           : static_cast<uint8_t>(
                                 (blockSums[component] + colorWeight / 2) /
                                 colorWeight);
    }
    if (lastComponentIsAlpha) {
      output[colorComponents] = static_cast<uint8_t>(
          (blockSums[colorComponents] + pixelCount / 2) / pixelCount);
    }
    output += components;
    blockSums += components;
  }
  std::fill(sums.begin(), sums.end(), 0);
  addedRows = 0;
}
