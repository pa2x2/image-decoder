#ifndef IMAGEDECODER_INCREMENTAL_ANIMATION_CANVAS_H
#define IMAGEDECODER_INCREMENTAL_ANIMATION_CANVAS_H

#include "incremental/format_decoder.h"
#include "incremental/image_canvas.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

struct IncrementalAnimationRegion {
  uint32_t sourceLeft;
  uint32_t sourceTop;
  uint32_t sourceWidth;
  uint32_t sourceHeight;
  uint32_t outputLeft;
  uint32_t outputTop;
  uint32_t outputWidth;
  uint32_t outputHeight;
};

class IncrementalAnimationCanvas {
public:
  IncrementalAnimationCanvas(uint32_t sourceWidth, uint32_t sourceHeight,
                             const IncrementalDecodeOptionsNative& options,
                             std::array<uint8_t, 4> background = {0, 0, 0, 0});

  const IncrementalOutputDimensions& outputDimensions() const;
  IncrementalAnimationRegion mapRegion(uint32_t left, uint32_t top,
                                       uint32_t width, uint32_t height) const;

  void beginFrame(const IncrementalAnimationRegion& region,
                  IncrementalDisposalOperationNative disposal);
  void composite(const IncrementalAnimationRegion& region, const uint8_t* rgba,
                 IncrementalBlendOperationNative blend);
  std::unique_ptr<IncrementalUpdate>
  makeFrameUpdate(ImageFormat format, int32_t frameIndex,
                  uint64_t durationMillis,
                  const IncrementalAnimationRegion& region,
                  IncrementalBlendOperationNative blend,
                  IncrementalDisposalOperationNative disposal);
  void disposeFrame(const IncrementalAnimationRegion& region,
                    IncrementalDisposalOperationNative disposal);

private:
  uint32_t mapStart(uint32_t coordinate, uint32_t sourceSize,
                    uint32_t outputSize) const;
  uint32_t mapEnd(uint32_t coordinate, uint32_t extent, uint32_t sourceSize,
                  uint32_t outputSize) const;
  void clearRegion(const IncrementalAnimationRegion& region);

  uint32_t sourceWidthValue;
  uint32_t sourceHeightValue;
  IncrementalOutputDimensions outputDimensionsValue;
  std::array<uint8_t, 4> backgroundValue;
  std::vector<uint8_t> pixels;
  std::vector<uint8_t> previousPixels;
  uint64_t generation = 0;
};

#endif // IMAGEDECODER_INCREMENTAL_ANIMATION_CANVAS_H
