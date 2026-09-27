#include "support/scaled_decode_expectations.h"

#include "incremental/area_resampling.h"

#include <string>

namespace {

struct ScaledDecodePair {
  IncrementalDecodeTrace fullSize;
  IncrementalDecodeTrace scaled;
  uint32_t outputHeight;
};

ScaledDecodePair decode_full_size_and_scaled(
    const IncrementalDecoderFactory& factory,
    const std::vector<uint8_t>& encoded, uint32_t width, uint32_t height,
    uint32_t outputWidth, size_t chunkSize, std::string_view description) {
  ScaledDecodePair pair{
      .fullSize = decode_in_chunks(factory, full_size_options(width, height),
                                   encoded, chunkSize),
      .scaled = decode_in_chunks(
          factory, scaled_options(width, height, outputWidth), encoded,
          chunkSize),
      .outputHeight = 0,
  };
  require_condition(
      pair.scaled.result == IncrementalBackendResult::Complete &&
          pair.scaled.metadata.has_value() &&
          pair.scaled.metadata->outputWidth == outputWidth,
      std::string(description) + " must decode at the requested output width");
  pair.outputHeight = pair.scaled.metadata->outputHeight;
  return pair;
}

std::vector<uint8_t> area_downscaled(const std::vector<uint8_t>& rgba,
                                     uint32_t width, uint32_t height,
                                     uint32_t outputWidth,
                                     uint32_t outputHeight) {
  require_condition(rgba.size() == static_cast<size_t>(width) * height * 4,
                    "area reference source must be a complete RGBA image");
  std::vector<uint8_t> output(static_cast<size_t>(outputWidth) * outputHeight *
                              4);
  resample_area_rect(rgba.data(), AreaAxis(width, outputWidth),
                     AreaAxis(height, outputHeight),
                     AreaOutputRect{.left = 0,
                                    .top = 0,
                                    .right = outputWidth,
                                    .bottom = outputHeight},
                     output.data());
  return output;
}

} // namespace

void require_scaled_still_is_area_average(
    const IncrementalDecoderFactory& factory,
    const std::vector<uint8_t>& encoded, uint32_t width, uint32_t height,
    uint32_t outputWidth, size_t chunkSize, std::string_view description) {
  const auto pair = decode_full_size_and_scaled(
      factory, encoded, width, height, outputWidth, chunkSize, description);
  require_condition(!pair.fullSize.stillUpdates.empty() &&
                        !pair.scaled.stillUpdates.empty(),
                    std::string(description) + " must publish stills");
  require_condition(
      pair.scaled.stillUpdates.back().rgba ==
          area_downscaled(pair.fullSize.stillUpdates.back().rgba, width,
                          height, outputWidth, pair.outputHeight),
      std::string(description) + " must publish the area average of its pixels");
}

void require_scaled_frames_are_area_averages(
    const IncrementalDecoderFactory& factory,
    const std::vector<uint8_t>& encoded, uint32_t width, uint32_t height,
    uint32_t outputWidth, size_t chunkSize, std::string_view description) {
  const auto pair = decode_full_size_and_scaled(
      factory, encoded, width, height, outputWidth, chunkSize, description);
  const auto& fullSizeFrames = pair.fullSize.animationFrames;
  const auto& scaledFrames = pair.scaled.animationFrames;
  require_condition(!scaledFrames.empty() &&
                        scaledFrames.size() == fullSizeFrames.size(),
                    std::string(description) + " must publish every frame");
  for (size_t index = 0; index < scaledFrames.size(); ++index) {
    require_condition(
        scaledFrames[index].rgba ==
            area_downscaled(fullSizeFrames[index].rgba, width, height,
                            outputWidth, pair.outputHeight),
        std::string(description) +
            " frames must be area averages of the composed frames");
  }
}
