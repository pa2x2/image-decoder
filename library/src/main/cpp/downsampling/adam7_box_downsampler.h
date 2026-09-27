#ifndef IMAGEDECODER_ADAM7_BOX_DOWNSAMPLER_H
#define IMAGEDECODER_ADAM7_BOX_DOWNSAMPLER_H

#include "downsampling/box_block_sums.h"
#include "rect.h"

#include <array>
#include <cstdint>
#include <variant>

// Averages every pixel of each sampleSize x sampleSize block of a region of an
// Adam7-interlaced image, taking the pixels from the reduced rows of each pass
// as libpng returns them without interlace handling. Every source pixel belongs
// to exactly one pass, so it is added to its block as soon as its pass row
// arrives. The blocks complete only with the last pass, which is why their sums
// are kept for the whole output, but the source region itself never is.
class Adam7BoxDownsampler {
public:
  static constexpr int kPasses = 7;

  // `inRect` starts at the first source pixel of the region, and `outRect` is
  // its downsampled size.
  Adam7BoxDownsampler(Rect inRect, Rect outRect, uint32_t sampleSize,
                      uint32_t components, bool lastComponentIsAlpha);

  // Whether any pixel of the reduced row lies inside the region.
  bool coversPassRow(int pass, uint32_t passRow) const;
  // `row` is the whole reduced row `passRow` of `pass`, which must cover the
  // region.
  void addPassRow(int pass, uint32_t passRow, const uint8_t* row);
  // Writes the average of every block once all passes have been added.
  void write(uint8_t* output) const;

private:
  uint32_t regionY;
  uint32_t regionEndY;
  uint32_t regionX;
  uint32_t outputWidth;
  uint32_t outputHeight;
  uint32_t sampleSize;
  uint32_t components;
  // The reduced columns of each pass that lie inside the region.
  std::array<uint32_t, kPasses> firstColumns;
  std::array<uint32_t, kPasses> endColumns;
  // The sums are held for every output block until the last pass, so they are
  // no wider than the total of one block needs.
  std::variant<BoxBlockSums<uint32_t>, BoxBlockSums<uint64_t>> blocks;
};

#endif // IMAGEDECODER_ADAM7_BOX_DOWNSAMPLER_H
