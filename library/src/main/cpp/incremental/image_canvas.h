#ifndef IMAGEDECODER_INCREMENTAL_IMAGE_CANVAS_H
#define IMAGEDECODER_INCREMENTAL_IMAGE_CANVAS_H

#include "incremental/format_decoder.h"

#include <cstdint>
#include <memory>
#include <vector>

struct IncrementalOutputDimensions {
  uint32_t width;
  uint32_t height;
};

IncrementalOutputDimensions calculate_incremental_output_dimensions(
    uint32_t sourceWidth, uint32_t sourceHeight,
    const IncrementalDecodeOptionsNative& options);

class IncrementalImageCanvas {
public:
  IncrementalImageCanvas(uint32_t sourceWidth, uint32_t sourceHeight,
                         IncrementalOutputDimensions outputDimensions);

  uint32_t outputWidth() const;
  uint32_t outputHeight() const;
  uint32_t sourceXForOutput(uint32_t outputX) const;
  bool outputRowForSource(uint32_t sourceY, uint32_t* outputY) const;

  void updatePixel(uint32_t outputX, uint32_t outputY, const uint8_t* rgba,
                   bool overwrite);
  void updateSourceRow(uint32_t sourceY, const uint8_t* rgbaSourceRow);
  std::unique_ptr<IncrementalPixelSnapshot> takeSnapshot();

private:
  void markDirty(uint32_t x, uint32_t y);

  uint32_t sourceWidthValue;
  uint32_t sourceHeightValue;
  uint32_t outputWidthValue;
  uint32_t outputHeightValue;
  std::shared_ptr<std::vector<uint8_t>> pixels;
  std::vector<uint8_t> initializedPixels;
  uint64_t generation = 0;
  uint32_t dirtyLeft = 0;
  uint32_t dirtyTop = 0;
  uint32_t dirtyRight = 0;
  uint32_t dirtyBottom = 0;
  bool dirty = false;
};

#endif // IMAGEDECODER_INCREMENTAL_IMAGE_CANVAS_H
