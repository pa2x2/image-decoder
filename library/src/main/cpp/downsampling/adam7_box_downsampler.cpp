#include "downsampling/adam7_box_downsampler.h"

#include "png.h"

#include <stdexcept>

static_assert(Adam7BoxDownsampler::kPasses == PNG_INTERLACE_ADAM7_PASSES);

namespace {

// The first reduced column of `pass` whose image column is at least `x`.
uint32_t firstPassColumnFrom(uint32_t x, int pass) {
  const uint32_t start = PNG_PASS_START_COL(pass);
  const uint32_t shift = PNG_PASS_COL_SHIFT(pass);
  return x <= start ? 0 : (x - start + (1u << shift) - 1) >> shift;
}

std::variant<BoxBlockSums<uint32_t>, BoxBlockSums<uint64_t>>
makeBlocks(size_t blockCount, uint32_t sampleSize, uint32_t components,
           bool lastComponentIsAlpha) {
  if (BoxBlockSums<uint32_t>::holdsBlocksOf(sampleSize, lastComponentIsAlpha)) {
    return BoxBlockSums<uint32_t>(blockCount, sampleSize, components,
                                  lastComponentIsAlpha);
  }
  return BoxBlockSums<uint64_t>(blockCount, sampleSize, components,
                                lastComponentIsAlpha);
}

} // namespace

Adam7BoxDownsampler::Adam7BoxDownsampler(Rect inRect, Rect outRect,
                                         uint32_t sampleSize,
                                         uint32_t components,
                                         bool lastComponentIsAlpha)
    : regionY(inRect.y), regionEndY(inRect.y + outRect.height * sampleSize),
      regionX(inRect.x), outputWidth(outRect.width),
      outputHeight(outRect.height), sampleSize(sampleSize),
      components(components),
      blocks(makeBlocks(static_cast<size_t>(outRect.width) * outRect.height,
                        sampleSize, components, lastComponentIsAlpha)) {
  const uint32_t regionEndX = inRect.x + outRect.width * sampleSize;
  for (int pass = 0; pass < kPasses; ++pass) {
    firstColumns[pass] = firstPassColumnFrom(regionX, pass);
    endColumns[pass] = firstPassColumnFrom(regionEndX, pass);
  }
}

bool Adam7BoxDownsampler::coversPassRow(int pass, uint32_t passRow) const {
  const uint32_t y = PNG_ROW_FROM_PASS_ROW(passRow, pass);
  return y >= regionY && y < regionEndY &&
         firstColumns[pass] < endColumns[pass];
}

void Adam7BoxDownsampler::addPassRow(int pass, uint32_t passRow,
                                     const uint8_t* row) {
  if (!coversPassRow(pass, passRow)) {
    throw std::logic_error("Interlaced pass row is outside the region");
  }
  const uint32_t y = PNG_ROW_FROM_PASS_ROW(passRow, pass);
  const size_t rowBlocks =
      static_cast<size_t>((y - regionY) / sampleSize) * outputWidth;
  std::visit(
      [&](auto& sums) {
        for (uint32_t column = firstColumns[pass]; column < endColumns[pass];
             ++column) {
          const uint32_t x = PNG_COL_FROM_PASS_COL(column, pass);
          sums.add(rowBlocks + (x - regionX) / sampleSize,
                   row + static_cast<size_t>(column) * components);
        }
      },
      blocks);
}

void Adam7BoxDownsampler::write(uint8_t* output) const {
  const size_t blockCount = static_cast<size_t>(outputWidth) * outputHeight;
  std::visit(
      [&](const auto& sums) {
        for (size_t block = 0; block < blockCount; ++block) {
          sums.resolve(block, output + block * components);
        }
      },
      blocks);
}
