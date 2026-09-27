#include "incremental/animation_canvas.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace {

constexpr uint64_t kMaximumAnimationSourcePixels = 32 * 1024 * 1024;

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

void blend_pixel(const uint8_t* source, uint8_t* destination,
                 IncrementalBlendOperationNative blend) {
  if (blend == IncrementalBlendOperationNative::Source || source[3] == 255) {
    std::memcpy(destination, source, 4);
    return;
  }
  if (source[3] == 0) {
    return;
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

} // namespace

IncrementalAnimationCanvas::IncrementalAnimationCanvas(
    uint32_t sourceWidth, uint32_t sourceHeight,
    const IncrementalDecodeOptionsNative& options,
    std::array<uint8_t, 4> background)
    : sourceWidthValue(sourceWidth), sourceHeightValue(sourceHeight),
      outputDimensionsValue(calculate_incremental_output_dimensions(
          sourceWidth, sourceHeight, options)),
      xAxis(sourceWidth, outputDimensionsValue.width),
      yAxis(sourceHeight, outputDimensionsValue.height),
      backgroundValue(background) {
  if (!fits(sourceWidth, sourceHeight, options)) {
    throw std::runtime_error("Animation canvas exceeds incremental limits");
  }
  const auto whole = mapRegion(0, 0, sourceWidth, sourceHeight);
  sourcePixels.resize(static_cast<size_t>(sourceWidth) * sourceHeight * 4);
  if (!xAxis.isIdentity() || !yAxis.isIdentity()) {
    scaledPixels.resize(static_cast<size_t>(outputDimensionsValue.width) *
                        outputDimensionsValue.height * 4);
  }
  fillRegion(whole);
  refreshOutput(whole);
}

bool IncrementalAnimationCanvas::fits(
    uint32_t sourceWidth, uint32_t sourceHeight,
    const IncrementalDecodeOptionsNative& options) {
  const uint64_t sourcePixels = static_cast<uint64_t>(sourceWidth) * sourceHeight;
  return sourcePixels <= std::min<uint64_t>(kMaximumAnimationSourcePixels,
                                            options.maximumBitmapPixels * 2);
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
  const auto output = area_output_rect(xAxis, yAxis, left, top, width, height);
  return {.sourceLeft = left,
          .sourceTop = top,
          .sourceWidth = width,
          .sourceHeight = height,
          .outputLeft = output.left,
          .outputTop = output.top,
          .outputWidth = output.right - output.left,
          .outputHeight = output.bottom - output.top};
}

void IncrementalAnimationCanvas::beginFrame(
    const IncrementalAnimationRegion& region,
    IncrementalDisposalOperationNative disposal) {
  previousRegionPixels.clear();
  if (disposal != IncrementalDisposalOperationNative::Previous) {
    return;
  }
  const size_t regionStride = static_cast<size_t>(region.sourceWidth) * 4;
  previousRegionPixels.resize(regionStride * region.sourceHeight);
  for (uint32_t y = 0; y < region.sourceHeight; ++y) {
    const size_t sourceOffset =
        (static_cast<size_t>(region.sourceTop + y) * sourceWidthValue +
         region.sourceLeft) *
        4;
    std::memcpy(previousRegionPixels.data() + y * regionStride,
                sourcePixels.data() + sourceOffset, regionStride);
  }
}

void IncrementalAnimationCanvas::composite(
    const IncrementalAnimationRegion& region, const uint8_t* rgba,
    IncrementalBlendOperationNative blend) {
  if (rgba == nullptr) {
    throw std::runtime_error("Animation frame pixels are unavailable");
  }
  for (uint32_t y = 0; y < region.sourceHeight; ++y) {
    const uint8_t* sourceRow =
        rgba + static_cast<size_t>(y) * region.sourceWidth * 4;
    uint8_t* destinationRow =
        sourcePixels.data() +
        (static_cast<size_t>(region.sourceTop + y) * sourceWidthValue +
         region.sourceLeft) *
            4;
    for (uint32_t x = 0; x < region.sourceWidth; ++x) {
      blend_pixel(sourceRow + static_cast<size_t>(x) * 4,
                  destinationRow + static_cast<size_t>(x) * 4, blend);
    }
  }
  refreshOutput(region);
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
          .rgba = std::make_shared<std::vector<uint8_t>>(outputPixels()),
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
    fillRegion(region);
    break;
  case IncrementalDisposalOperationNative::Previous:
    if (previousRegionPixels.empty()) {
      fillRegion(region);
      break;
    }
    for (uint32_t y = 0; y < region.sourceHeight; ++y) {
      const size_t regionStride = static_cast<size_t>(region.sourceWidth) * 4;
      const size_t sourceOffset =
          (static_cast<size_t>(region.sourceTop + y) * sourceWidthValue +
           region.sourceLeft) *
          4;
      std::memcpy(sourcePixels.data() + sourceOffset,
                  previousRegionPixels.data() + y * regionStride, regionStride);
    }
    previousRegionPixels.clear();
    break;
  }
  refreshOutput(region);
}

void IncrementalAnimationCanvas::fillRegion(
    const IncrementalAnimationRegion& region) {
  for (uint32_t y = 0; y < region.sourceHeight; ++y) {
    uint8_t* row =
        sourcePixels.data() +
        (static_cast<size_t>(region.sourceTop + y) * sourceWidthValue +
         region.sourceLeft) *
            4;
    for (uint32_t x = 0; x < region.sourceWidth; ++x) {
      std::copy(backgroundValue.begin(), backgroundValue.end(),
                row + static_cast<size_t>(x) * 4);
    }
  }
}

void IncrementalAnimationCanvas::refreshOutput(
    const IncrementalAnimationRegion& region) {
  if (scaledPixels.empty()) {
    return;
  }
  resample_area_rect(
      sourcePixels.data(), xAxis, yAxis,
      AreaOutputRect{.left = region.outputLeft,
                     .top = region.outputTop,
                     .right = region.outputLeft + region.outputWidth,
                     .bottom = region.outputTop + region.outputHeight},
      scaledPixels.data());
}

const std::vector<uint8_t>& IncrementalAnimationCanvas::outputPixels() const {
  return scaledPixels.empty() ? sourcePixels : scaledPixels;
}
