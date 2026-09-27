#include "downsampling/box_downsampler.h"

#include <stdexcept>

BoxDownsampler::BoxDownsampler(uint32_t outputWidth, uint32_t sampleSize,
                               uint32_t components, bool lastComponentIsAlpha)
    : outputWidth(outputWidth), sampleSize(sampleSize), components(components),
      blocks(outputWidth, sampleSize, components, lastComponentIsAlpha) {}

void BoxDownsampler::addRow(const uint8_t* row) {
  if (addedRows == sampleSize) {
    throw std::logic_error("Downsampled block already has all its rows");
  }
  ++addedRows;
  for (uint32_t block = 0; block < outputWidth; ++block) {
    for (uint32_t pixel = 0; pixel < sampleSize; ++pixel) {
      blocks.add(block, row);
      row += components;
    }
  }
}

void BoxDownsampler::writeRow(uint8_t* output) {
  if (addedRows != sampleSize) {
    throw std::logic_error("Downsampled block is missing rows");
  }
  for (uint32_t block = 0; block < outputWidth; ++block) {
    blocks.resolve(block, output);
    output += components;
  }
  blocks.clear();
  addedRows = 0;
}
