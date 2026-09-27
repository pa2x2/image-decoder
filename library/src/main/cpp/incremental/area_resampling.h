#ifndef IMAGEDECODER_INCREMENTAL_AREA_RESAMPLING_H
#define IMAGEDECODER_INCREMENTAL_AREA_RESAMPLING_H

#include <cstddef>
#include <cstdint>

// The output pixels one source pixel contributes to along an axis.
struct AreaTap {
  uint32_t output;
  uint32_t firstWeight;
  // Weight in `output + 1`; zero when the source pixel ends inside `output`.
  uint32_t secondWeight;
};

// Maps one axis of a source grid onto an output grid that is not larger.
// Output pixel `o` covers the source span [o * S / O, (o + 1) * S / O). Weights
// are overlaps measured in units of 1 / O source pixels, so every output
// footprint weighs exactly S and each source pixel reaches at most two outputs.
class AreaAxis {
public:
  AreaAxis(uint32_t sourceSize, uint32_t outputSize);

  uint32_t sourceSize() const;
  uint32_t outputSize() const;
  bool isIdentity() const;

  AreaTap tapOf(uint32_t source) const;
  uint32_t firstSourceOf(uint32_t output) const;
  uint32_t endSourceOf(uint32_t output) const;
  uint32_t firstOutputOf(uint32_t source) const;
  uint32_t endOutputCovering(uint32_t sourceEnd) const;

private:
  uint32_t sourceSizeValue;
  uint32_t outputSizeValue;
};

// Alpha-weighted channel sums of the source pixels that fall in one output
// pixel. Colors are averaged by coverage, so transparent pixels do not darken
// their neighbours.
struct AreaPixelSums {
  uint64_t alpha = 0;
  uint64_t red = 0;
  uint64_t green = 0;
  uint64_t blue = 0;
};

// Adds `rowWeight` times the horizontal footprint sums of a straight-alpha
// RGBA source row to output columns [firstOutputX, firstOutputX + count).
// `sourceRow` points at source column 0.
void accumulate_area_row(const AreaAxis& xAxis, const uint8_t* sourceRow,
                         uint64_t rowWeight, uint32_t firstOutputX,
                         AreaPixelSums* sums, size_t count);

// Resolves sums collected over a footprint weighing `weight` into straight
// RGBA.
void resolve_area_pixel(const AreaPixelSums& sums, uint64_t weight,
                        uint8_t* rgba);

struct AreaOutputRect {
  uint32_t left;
  uint32_t top;
  uint32_t right;
  uint32_t bottom;
};

// Output pixels whose footprints overlap the given source rectangle.
AreaOutputRect area_output_rect(const AreaAxis& xAxis, const AreaAxis& yAxis,
                                uint32_t sourceLeft, uint32_t sourceTop,
                                uint32_t sourceWidth, uint32_t sourceHeight);

// Recomputes `rect` of a tightly packed output image from a complete, tightly
// packed straight-alpha RGBA source image.
void resample_area_rect(const uint8_t* source, const AreaAxis& xAxis,
                        const AreaAxis& yAxis, const AreaOutputRect& rect,
                        uint8_t* output);

#endif // IMAGEDECODER_INCREMENTAL_AREA_RESAMPLING_H
