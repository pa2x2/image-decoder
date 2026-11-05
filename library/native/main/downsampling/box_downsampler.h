#ifndef IMAGEDECODER_BOX_DOWNSAMPLER_H
#define IMAGEDECODER_BOX_DOWNSAMPLER_H

#include "downsampling/box_block_sums.h"

#include <cstdint>

// Averages every pixel of each sampleSize x sampleSize block of interleaved
// 8-bit pixels. Decoders add the block's source rows one at a time, so rows can
// be streamed from the codec and only one row of blocks is summed at once.
class BoxDownsampler {
public:
  BoxDownsampler(uint32_t outputWidth, uint32_t sampleSize, uint32_t components,
                 bool lastComponentIsAlpha);

  // `row` points at the first source pixel of the downsampled region.
  void addRow(const uint8_t* row);
  // Writes the average of the rows added since the previous call.
  void writeRow(uint8_t* output);

private:
  uint32_t outputWidth;
  uint32_t sampleSize;
  uint32_t components;
  uint32_t addedRows = 0;
  BoxBlockSums<uint64_t> blocks;
};

#endif // IMAGEDECODER_BOX_DOWNSAMPLER_H
