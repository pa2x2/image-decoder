#include "incremental/area_resampling.h"

#include <algorithm>
#include <stdexcept>
#include <vector>

AreaAxis::AreaAxis(uint32_t sourceSize, uint32_t outputSize)
    : sourceSizeValue(sourceSize), outputSizeValue(outputSize) {
  if (outputSize == 0 || outputSize > sourceSize) {
    throw std::runtime_error("Area resampling cannot enlarge an axis");
  }
}

uint32_t AreaAxis::sourceSize() const { return sourceSizeValue; }

uint32_t AreaAxis::outputSize() const { return outputSizeValue; }

bool AreaAxis::isIdentity() const {
  return sourceSizeValue == outputSizeValue;
}

AreaTap AreaAxis::tapOf(uint32_t source) const {
  const uint64_t spanStart = static_cast<uint64_t>(source) * outputSizeValue;
  const uint64_t spanEnd = spanStart + outputSizeValue;
  const uint64_t output = spanStart / sourceSizeValue;
  const uint64_t boundary = (output + 1) * sourceSizeValue;
  if (spanEnd <= boundary) {
    return {.output = static_cast<uint32_t>(output),
            .firstWeight = outputSizeValue,
            .secondWeight = 0};
  }
  return {.output = static_cast<uint32_t>(output),
          .firstWeight = static_cast<uint32_t>(boundary - spanStart),
          .secondWeight = static_cast<uint32_t>(spanEnd - boundary)};
}

uint32_t AreaAxis::firstSourceOf(uint32_t output) const {
  return static_cast<uint32_t>(static_cast<uint64_t>(output) * sourceSizeValue /
                               outputSizeValue);
}

uint32_t AreaAxis::endSourceOf(uint32_t output) const {
  const uint64_t end = (static_cast<uint64_t>(output) + 1) * sourceSizeValue;
  return static_cast<uint32_t>((end + outputSizeValue - 1) / outputSizeValue);
}

uint32_t AreaAxis::firstOutputOf(uint32_t source) const {
  return static_cast<uint32_t>(static_cast<uint64_t>(source) * outputSizeValue /
                               sourceSizeValue);
}

uint32_t AreaAxis::endOutputCovering(uint32_t sourceEnd) const {
  const uint64_t end = static_cast<uint64_t>(sourceEnd) * outputSizeValue;
  return static_cast<uint32_t>((end + sourceSizeValue - 1) / sourceSizeValue);
}

namespace {

void add_weighted(AreaPixelSums& sums, const uint8_t* rgba, uint64_t weight) {
  const uint64_t weightedAlpha = weight * rgba[3];
  sums.alpha += weightedAlpha;
  sums.red += weightedAlpha * rgba[0];
  sums.green += weightedAlpha * rgba[1];
  sums.blue += weightedAlpha * rgba[2];
}

uint8_t rounded_quotient(uint64_t numerator, uint64_t denominator) {
  return static_cast<uint8_t>((numerator + denominator / 2) / denominator);
}

} // namespace

void accumulate_area_row(const AreaAxis& xAxis, const uint8_t* sourceRow,
                         uint64_t rowWeight, uint32_t firstOutputX,
                         AreaPixelSums* sums, size_t count) {
  if (count == 0) {
    return;
  }
  const uint32_t endOutputX = firstOutputX + static_cast<uint32_t>(count);
  const uint32_t endSourceX = xAxis.endSourceOf(endOutputX - 1);
  for (uint32_t sourceX = xAxis.firstSourceOf(firstOutputX);
       sourceX < endSourceX; ++sourceX) {
    const uint8_t* pixel = sourceRow + static_cast<size_t>(sourceX) * 4;
    const AreaTap tap = xAxis.tapOf(sourceX);
    if (tap.output >= firstOutputX) {
      add_weighted(sums[tap.output - firstOutputX], pixel,
                   rowWeight * tap.firstWeight);
    }
    if (tap.secondWeight != 0 && tap.output + 1 < endOutputX) {
      add_weighted(sums[tap.output + 1 - firstOutputX], pixel,
                   rowWeight * tap.secondWeight);
    }
  }
}

void resolve_area_pixel(const AreaPixelSums& sums, uint64_t weight,
                        uint8_t* rgba) {
  if (sums.alpha == 0) {
    std::fill(rgba, rgba + 4, 0);
    return;
  }
  rgba[0] = rounded_quotient(sums.red, sums.alpha);
  rgba[1] = rounded_quotient(sums.green, sums.alpha);
  rgba[2] = rounded_quotient(sums.blue, sums.alpha);
  rgba[3] = rounded_quotient(sums.alpha, weight);
}

AreaOutputRect area_output_rect(const AreaAxis& xAxis, const AreaAxis& yAxis,
                                uint32_t sourceLeft, uint32_t sourceTop,
                                uint32_t sourceWidth, uint32_t sourceHeight) {
  return {.left = xAxis.firstOutputOf(sourceLeft),
          .top = yAxis.firstOutputOf(sourceTop),
          .right = xAxis.endOutputCovering(sourceLeft + sourceWidth),
          .bottom = yAxis.endOutputCovering(sourceTop + sourceHeight)};
}

void resample_area_rect(const uint8_t* source, const AreaAxis& xAxis,
                        const AreaAxis& yAxis, const AreaOutputRect& rect,
                        uint8_t* output) {
  const size_t sourceStride = static_cast<size_t>(xAxis.sourceSize()) * 4;
  const size_t outputStride = static_cast<size_t>(xAxis.outputSize()) * 4;
  const uint64_t footprintWeight =
      static_cast<uint64_t>(xAxis.sourceSize()) * yAxis.sourceSize();
  std::vector<AreaPixelSums> sums(rect.right - rect.left);
  for (uint32_t outputY = rect.top; outputY < rect.bottom; ++outputY) {
    std::fill(sums.begin(), sums.end(), AreaPixelSums{});
    const uint32_t endSourceY = yAxis.endSourceOf(outputY);
    for (uint32_t sourceY = yAxis.firstSourceOf(outputY); sourceY < endSourceY;
         ++sourceY) {
      const AreaTap tap = yAxis.tapOf(sourceY);
      const uint32_t rowWeight =
          tap.output == outputY ? tap.firstWeight : tap.secondWeight;
      accumulate_area_row(xAxis, source + sourceY * sourceStride, rowWeight,
                          rect.left, sums.data(), sums.size());
    }
    uint8_t* outputRow = output + outputY * outputStride;
    for (uint32_t outputX = rect.left; outputX < rect.right; ++outputX) {
      resolve_area_pixel(sums[outputX - rect.left], footprintWeight,
                         outputRow + static_cast<size_t>(outputX) * 4);
    }
  }
}
