#include "incremental/image_canvas.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace {

uint32_t sampled_source_coordinate(uint32_t outputCoordinate,
                                   uint32_t outputSize, uint32_t sourceSize) {
  const uint64_t numerator =
      (static_cast<uint64_t>(outputCoordinate) * 2 + 1) * sourceSize;
  return std::min<uint32_t>(
      sourceSize - 1, numerator / (static_cast<uint64_t>(outputSize) * 2));
}

} // namespace

IncrementalOutputDimensions calculate_incremental_output_dimensions(
    uint32_t sourceWidth, uint32_t sourceHeight,
    const IncrementalDecodeOptionsNative& options) {
  if (sourceWidth == 0 || sourceHeight == 0) {
    throw std::runtime_error("Image dimensions must be positive");
  }
  if (sourceWidth > kMaximumIncrementalSourceDimension ||
      sourceHeight > kMaximumIncrementalSourceDimension) {
    throw std::runtime_error("Image dimensions exceed incremental limits");
  }

  const long double sourcePixels =
      static_cast<long double>(sourceWidth) * sourceHeight;
  const long double widthScale = std::min<long double>(
      1, static_cast<long double>(options.preferredOutputWidth) / sourceWidth);
  const long double heightScale = std::min<long double>(
      1, static_cast<long double>(kMaximumIncrementalOutputDimension) /
             sourceHeight);
  const long double pixelScale = std::min<long double>(
      1, std::sqrt(options.maximumBitmapPixels / sourcePixels));
  const long double scale = std::min({widthScale, heightScale, pixelScale});

  uint32_t width = std::max<uint32_t>(1, std::floor(sourceWidth * scale));
  uint32_t height = std::max<uint32_t>(1, std::floor(sourceHeight * scale));
  while (static_cast<uint64_t>(width) * height > options.maximumBitmapPixels) {
    if (width >= height && width > 1) {
      --width;
    } else if (height > 1) {
      --height;
    } else {
      throw std::runtime_error("Unable to satisfy incremental bitmap bounds");
    }
  }
  return {.width = width, .height = height};
}

IncrementalImageCanvas::IncrementalImageCanvas(
    uint32_t sourceWidth, uint32_t sourceHeight,
    IncrementalOutputDimensions outputDimensions)
    : sourceWidthValue(sourceWidth), sourceHeightValue(sourceHeight),
      outputWidthValue(outputDimensions.width),
      outputHeightValue(outputDimensions.height),
      pixels(std::make_shared<std::vector<uint8_t>>(
          static_cast<size_t>(outputWidthValue) * outputHeightValue * 4)),
      initializedPixels(static_cast<size_t>(outputWidthValue) *
                        outputHeightValue),
      rowResampler(sourceWidth, sourceHeight, outputDimensions.width,
                   outputDimensions.height) {}

uint32_t IncrementalImageCanvas::outputWidth() const {
  return outputWidthValue;
}

uint32_t IncrementalImageCanvas::outputHeight() const {
  return outputHeightValue;
}

uint32_t IncrementalImageCanvas::sourceXForOutput(uint32_t outputX) const {
  return sampled_source_coordinate(outputX, outputWidthValue, sourceWidthValue);
}

bool IncrementalImageCanvas::outputRowForSource(uint32_t sourceY,
                                                uint32_t* outputY) const {
  uint32_t low = 0;
  uint32_t high = outputHeightValue;
  while (low < high) {
    const uint32_t middle = low + (high - low) / 2;
    if (sampled_source_coordinate(middle, outputHeightValue,
                                  sourceHeightValue) < sourceY) {
      low = middle + 1;
    } else {
      high = middle;
    }
  }
  if (low >= outputHeightValue ||
      sampled_source_coordinate(low, outputHeightValue, sourceHeightValue) !=
          sourceY) {
    return false;
  }
  *outputY = low;
  return true;
}

void IncrementalImageCanvas::updatePixel(uint32_t outputX, uint32_t outputY,
                                         const uint8_t* rgba, bool overwrite) {
  const size_t pixelIndex =
      static_cast<size_t>(outputY) * outputWidthValue + outputX;
  if (!overwrite && initializedPixels[pixelIndex] != 0) {
    return;
  }
  std::memcpy(pixels->data() + pixelIndex * 4, rgba, 4);
  initializedPixels[pixelIndex] = 1;
  markDirty(outputX, outputY, outputX + 1, outputY + 1);
}

void IncrementalImageCanvas::updateSourceRow(uint32_t sourceY,
                                             const uint8_t* rgbaSourceRow) {
  rowResampler.addRow(sourceY, rgbaSourceRow,
                      [this](uint32_t outputY, const uint8_t* rgba) {
                        writeOutputRow(outputY, rgba);
                      });
}

void IncrementalImageCanvas::writeOutputRow(uint32_t outputY,
                                            const uint8_t* rgba) {
  const size_t rowStart = static_cast<size_t>(outputY) * outputWidthValue;
  std::memcpy(pixels->data() + rowStart * 4, rgba,
              static_cast<size_t>(outputWidthValue) * 4);
  std::fill_n(initializedPixels.begin() + static_cast<std::ptrdiff_t>(rowStart),
              outputWidthValue, 1);
  markDirty(0, outputY, outputWidthValue, outputY + 1);
}

std::unique_ptr<IncrementalPixelSnapshot>
IncrementalImageCanvas::takeSnapshot() {
  if (!dirty) {
    return nullptr;
  }
  auto snapshot = std::make_unique<IncrementalPixelSnapshot>(
      IncrementalPixelSnapshot{.width = outputWidthValue,
                               .height = outputHeightValue,
                               .left = dirtyLeft,
                               .top = dirtyTop,
                               .right = dirtyRight,
                               .bottom = dirtyBottom,
                               .generation = ++generation,
                               .rgba = pixels});
  dirty = false;
  return snapshot;
}

std::unique_ptr<IncrementalPixelSnapshot>
IncrementalImageCanvas::takeFullSnapshot() {
  auto snapshot =
      std::make_unique<IncrementalPixelSnapshot>(IncrementalPixelSnapshot{
          .width = outputWidthValue,
          .height = outputHeightValue,
          .left = 0,
          .top = 0,
          .right = outputWidthValue,
          .bottom = outputHeightValue,
          .generation = ++generation,
          .rgba = std::make_shared<std::vector<uint8_t>>(*pixels)});
  dirty = false;
  return snapshot;
}

void IncrementalImageCanvas::markDirty(uint32_t left, uint32_t top,
                                       uint32_t right, uint32_t bottom) {
  if (!dirty) {
    dirtyLeft = left;
    dirtyTop = top;
    dirtyRight = right;
    dirtyBottom = bottom;
    dirty = true;
    return;
  }
  dirtyLeft = std::min(dirtyLeft, left);
  dirtyTop = std::min(dirtyTop, top);
  dirtyRight = std::max(dirtyRight, right);
  dirtyBottom = std::max(dirtyBottom, bottom);
}
