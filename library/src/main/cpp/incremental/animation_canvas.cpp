#include "incremental/animation_canvas.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace {

uint8_t blend_channel(uint8_t source, uint8_t destination, uint32_t sourceAlpha,
                      uint32_t destinationAlpha, uint32_t outputAlpha) {
  if (outputAlpha == 0) {
    return 0;
  }
  const uint32_t destinationContribution =
      destinationAlpha * (255 - sourceAlpha);
  const uint32_t numerator =
      static_cast<uint32_t>(source) * sourceAlpha * 255 +
      static_cast<uint32_t>(destination) * destinationContribution;
  return static_cast<uint8_t>((numerator + outputAlpha / 2) / outputAlpha);
}

} // namespace

IncrementalAnimationCanvas::IncrementalAnimationCanvas(
    uint32_t sourceWidth, uint32_t sourceHeight,
    const IncrementalDecodeOptionsNative& options,
    std::array<uint8_t, 4> background)
    : sourceWidthValue(sourceWidth), sourceHeightValue(sourceHeight),
      outputDimensionsValue(calculate_incremental_output_dimensions(
          sourceWidth, sourceHeight, options)),
      backgroundValue(background),
      pixels(static_cast<size_t>(outputDimensionsValue.width) *
             outputDimensionsValue.height * 4) {
  for (size_t offset = 0; offset < pixels.size(); offset += 4) {
    std::copy(backgroundValue.begin(), backgroundValue.end(),
              pixels.begin() + offset);
  }
}

const IncrementalOutputDimensions&
IncrementalAnimationCanvas::outputDimensions() const {
  return outputDimensionsValue;
}

IncrementalAnimationRegion
IncrementalAnimationCanvas::mapRegion(uint32_t left, uint32_t top,
                                      uint32_t width, uint32_t height) const {
  if (width == 0 || height == 0 || width > sourceWidthValue ||
      height > sourceHeightValue || left > sourceWidthValue - width ||
      top > sourceHeightValue - height) {
    throw std::runtime_error("Animation frame exceeds canvas bounds");
  }
  const uint32_t outputLeft =
      mapStart(left, sourceWidthValue, outputDimensionsValue.width);
  const uint32_t outputTop =
      mapStart(top, sourceHeightValue, outputDimensionsValue.height);
  const uint32_t outputRight =
      mapEnd(left, width, sourceWidthValue, outputDimensionsValue.width);
  const uint32_t outputBottom =
      mapEnd(top, height, sourceHeightValue, outputDimensionsValue.height);
  return {.sourceLeft = left,
          .sourceTop = top,
          .sourceWidth = width,
          .sourceHeight = height,
          .outputLeft = outputLeft,
          .outputTop = outputTop,
          .outputWidth = outputRight - outputLeft,
          .outputHeight = outputBottom - outputTop};
}

void IncrementalAnimationCanvas::beginFrame(
    const IncrementalAnimationRegion&,
    IncrementalDisposalOperationNative disposal) {
  if (disposal == IncrementalDisposalOperationNative::Previous) {
    previousPixels = pixels;
  } else {
    previousPixels.clear();
  }
}

void IncrementalAnimationCanvas::composite(
    const IncrementalAnimationRegion& region, const uint8_t* rgba,
    IncrementalBlendOperationNative blend) {
  if (rgba == nullptr || region.outputWidth == 0 || region.outputHeight == 0) {
    throw std::runtime_error("Animation frame pixels are unavailable");
  }
  for (uint32_t y = 0; y < region.outputHeight; ++y) {
    for (uint32_t x = 0; x < region.outputWidth; ++x) {
      const size_t sourceOffset =
          (static_cast<size_t>(y) * region.outputWidth + x) * 4;
      const size_t destinationOffset =
          (static_cast<size_t>(region.outputTop + y) *
               outputDimensionsValue.width +
           region.outputLeft + x) *
          4;
      const uint8_t* source = rgba + sourceOffset;
      uint8_t* destination = pixels.data() + destinationOffset;
      if (blend == IncrementalBlendOperationNative::Source ||
          source[3] == 255) {
        std::memcpy(destination, source, 4);
        continue;
      }
      if (source[3] == 0) {
        continue;
      }

      const uint32_t sourceAlpha = source[3];
      const uint32_t destinationAlpha = destination[3];
      const uint32_t outputAlpha =
          sourceAlpha * 255 + destinationAlpha * (255 - sourceAlpha);
      destination[0] = blend_channel(source[0], destination[0], sourceAlpha,
                                     destinationAlpha, outputAlpha);
      destination[1] = blend_channel(source[1], destination[1], sourceAlpha,
                                     destinationAlpha, outputAlpha);
      destination[2] = blend_channel(source[2], destination[2], sourceAlpha,
                                     destinationAlpha, outputAlpha);
      destination[3] = static_cast<uint8_t>((outputAlpha + 127) / 255);
    }
  }
}

std::unique_ptr<IncrementalUpdate> IncrementalAnimationCanvas::makeFrameUpdate(
    ImageFormat format, int32_t frameIndex, uint64_t durationMillis,
    const IncrementalAnimationRegion& region,
    IncrementalBlendOperationNative blend,
    IncrementalDisposalOperationNative disposal) {
  auto update = std::make_unique<IncrementalUpdate>(IncrementalUpdate{
      .type = IncrementalUpdateType::AnimationFrameAvailable,
      .format = static_cast<int32_t>(format),
      .capabilities = 0,
      .info = nullptr,
      .snapshot = nullptr,
      .animationFrame = nullptr,
  });
  update->snapshot =
      std::make_unique<IncrementalPixelSnapshot>(IncrementalPixelSnapshot{
          .width = outputDimensionsValue.width,
          .height = outputDimensionsValue.height,
          .left = region.outputLeft,
          .top = region.outputTop,
          .right = region.outputLeft + region.outputWidth,
          .bottom = region.outputTop + region.outputHeight,
          .generation = ++generation,
          .rgba = std::make_shared<std::vector<uint8_t>>(pixels),
      });
  update->animationFrame = std::make_unique<IncrementalAnimationFrameNative>(
      IncrementalAnimationFrameNative{
          .index = frameIndex,
          .durationMillis = durationMillis,
          .blendOperation = blend,
          .disposalOperation = disposal,
      });
  return update;
}

void IncrementalAnimationCanvas::disposeFrame(
    const IncrementalAnimationRegion& region,
    IncrementalDisposalOperationNative disposal) {
  switch (disposal) {
  case IncrementalDisposalOperationNative::None:
    return;
  case IncrementalDisposalOperationNative::Background:
    clearRegion(region);
    return;
  case IncrementalDisposalOperationNative::Previous:
    if (previousPixels.size() == pixels.size()) {
      pixels.swap(previousPixels);
    } else {
      clearRegion(region);
    }
    previousPixels.clear();
    return;
  }
}

uint32_t IncrementalAnimationCanvas::mapStart(uint32_t coordinate,
                                              uint32_t sourceSize,
                                              uint32_t outputSize) const {
  return static_cast<uint32_t>(static_cast<uint64_t>(coordinate) * outputSize /
                               sourceSize);
}

uint32_t IncrementalAnimationCanvas::mapEnd(uint32_t coordinate,
                                            uint32_t extent,
                                            uint32_t sourceSize,
                                            uint32_t outputSize) const {
  const uint64_t numerator =
      static_cast<uint64_t>(coordinate + extent) * outputSize;
  return static_cast<uint32_t>(std::min<uint64_t>(
      outputSize, (numerator + sourceSize - 1) / sourceSize));
}

void IncrementalAnimationCanvas::clearRegion(
    const IncrementalAnimationRegion& region) {
  for (uint32_t y = 0; y < region.outputHeight; ++y) {
    for (uint32_t x = 0; x < region.outputWidth; ++x) {
      const size_t offset = (static_cast<size_t>(region.outputTop + y) *
                                 outputDimensionsValue.width +
                             region.outputLeft + x) *
                            4;
      std::copy(backgroundValue.begin(), backgroundValue.end(),
                pixels.begin() + offset);
    }
  }
}
