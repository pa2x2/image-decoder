#ifndef IMAGEDECODER_INCREMENTAL_ANIMATION_CANVAS_H
#define IMAGEDECODER_INCREMENTAL_ANIMATION_CANVAS_H

#include "incremental/area_resampling.h"
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
  // Output pixels whose area the source region contributes to.
  uint32_t outputLeft;
  uint32_t outputTop;
  uint32_t outputWidth;
  uint32_t outputHeight;
};

// Composes animation frames at source resolution, exactly as the format
// defines blending and disposal, and area-averages every changed region into
// the published output. Scaled output therefore never depends on how an
// encoder split frames into sub-rectangles.
class IncrementalAnimationCanvas {
public:
  IncrementalAnimationCanvas(uint32_t sourceWidth, uint32_t sourceHeight,
                             const IncrementalDecodeOptionsNative& options,
                             std::array<uint8_t, 4> background = {0, 0, 0, 0});

  // Whether the source-resolution composition buffer fits the memory the
  // options allow for one animation.
  static bool fits(uint32_t sourceWidth, uint32_t sourceHeight,
                   const IncrementalDecodeOptionsNative& options);

  const IncrementalOutputDimensions& outputDimensions() const;
  IncrementalAnimationRegion mapRegion(uint32_t left, uint32_t top,
                                       uint32_t width, uint32_t height) const;

  void beginFrame(const IncrementalAnimationRegion& region,
                  IncrementalDisposalOperationNative disposal);
  // `rgba` holds the region's straight-alpha source pixels, tightly packed.
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
  void fillRegion(const IncrementalAnimationRegion& region);
  void refreshOutput(const IncrementalAnimationRegion& region);
  const std::vector<uint8_t>& outputPixels() const;

  uint32_t sourceWidthValue;
  uint32_t sourceHeightValue;
  IncrementalOutputDimensions outputDimensionsValue;
  AreaAxis xAxis;
  AreaAxis yAxis;
  std::array<uint8_t, 4> backgroundValue;
  std::vector<uint8_t> sourcePixels;
  // Empty when the output keeps the source size and publishes sourcePixels.
  std::vector<uint8_t> scaledPixels;
  // The current frame's region as it was before compositing, kept only when
  // the frame is disposed by restoring it.
  std::vector<uint8_t> previousRegionPixels;
  uint64_t generation = 0;
};

#endif // IMAGEDECODER_INCREMENTAL_ANIMATION_CANVAS_H
