#include "incremental/area_row_resampler.h"

#include <algorithm>
#include <stdexcept>

IncrementalAreaRowResampler::IncrementalAreaRowResampler(uint32_t sourceWidth,
                                                         uint32_t sourceHeight,
                                                         uint32_t outputWidth,
                                                         uint32_t outputHeight)
    : xAxis(sourceWidth, outputWidth), yAxis(sourceHeight, outputHeight),
      sourceRowSums(outputWidth),
      resolvedRow(static_cast<size_t>(outputWidth) * 4) {
  for (auto& row : rowsInProgress) {
    row.sums.resize(outputWidth);
  }
}

void IncrementalAreaRowResampler::addRow(uint32_t sourceY, const uint8_t* rgba,
                                         const OutputRowSink& sink) {
  if (sourceY != nextSourceY) {
    throw std::runtime_error("Source rows must cover the image top to bottom");
  }
  nextSourceY = sourceY + 1 == yAxis.sourceSize() ? 0 : sourceY + 1;
  if (xAxis.isIdentity() && yAxis.isIdentity()) {
    sink(sourceY, rgba);
    return;
  }

  std::fill(sourceRowSums.begin(), sourceRowSums.end(), AreaPixelSums{});
  accumulate_area_row(xAxis, rgba, 1, 0, sourceRowSums.data(),
                      sourceRowSums.size());
  const AreaTap tap = yAxis.tapOf(sourceY);
  addToRow(tap.output, tap.firstWeight, sourceY, sink);
  if (tap.secondWeight != 0) {
    addToRow(tap.output + 1, tap.secondWeight, sourceY, sink);
  }
}

void IncrementalAreaRowResampler::addToRow(uint32_t outputY,
                                           uint32_t rowWeight,
                                           uint32_t sourceY,
                                           const OutputRowSink& sink) {
  RowSums& row = rowsInProgress[outputY % 2];
  if (sourceY == yAxis.firstSourceOf(outputY)) {
    row.receivedWeight = 0;
    std::fill(row.sums.begin(), row.sums.end(), AreaPixelSums{});
  }
  row.receivedWeight += rowWeight;
  const uint64_t receivedFootprint =
      static_cast<uint64_t>(xAxis.sourceSize()) * row.receivedWeight;
  for (size_t x = 0; x < row.sums.size(); ++x) {
    AreaPixelSums& target = row.sums[x];
    const AreaPixelSums& source = sourceRowSums[x];
    target.alpha += source.alpha * rowWeight;
    target.red += source.red * rowWeight;
    target.green += source.green * rowWeight;
    target.blue += source.blue * rowWeight;
    resolve_area_pixel(target, receivedFootprint, resolvedRow.data() + x * 4);
  }
  sink(outputY, resolvedRow.data());
}
