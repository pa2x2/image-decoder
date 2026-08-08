#include "support/decode_harness.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

IncrementalDecodeOptionsNative full_size_options(uint32_t width,
                                                 uint32_t height) {
  return IncrementalDecodeOptionsNative{
      .preferredOutputWidth = width,
      .maximumBitmapPixels = static_cast<uint64_t>(width) * height,
      .displayProfile = {},
  };
}

IncrementalDecodeTrace
decode_in_chunks(const IncrementalDecoderFactory& factory,
                 const IncrementalDecodeOptionsNative& options,
                 const std::vector<uint8_t>& encoded, size_t chunkSize) {
  require_condition(!encoded.empty(), "encoded fixture must not be empty");
  require_condition(chunkSize > 0, "chunk size must be positive");

  IncrementalDecodeTrace trace;
  auto decoder = factory(options);
  size_t offset = 0;
  size_t bytesAfterAppend = 0;
  bool endOfInput = false;
  const IncrementalUpdateSink sink = [&](IncrementalUpdate&& update) {
    if (update.type == IncrementalUpdateType::MetadataAvailable) {
      require_condition(update.info != nullptr,
                        "metadata update must carry image information");
      trace.metadata = *update.info;
    } else if (update.type == IncrementalUpdateType::StillImageAvailable) {
      require_condition(update.snapshot != nullptr,
                        "still update must carry a snapshot");
      require_condition(update.snapshot->rgba != nullptr,
                        "still snapshot must carry pixels");
      trace.stillUpdates.push_back(CapturedStillUpdate{
          .width = update.snapshot->width,
          .height = update.snapshot->height,
          .left = update.snapshot->left,
          .top = update.snapshot->top,
          .right = update.snapshot->right,
          .bottom = update.snapshot->bottom,
          .generation = update.snapshot->generation,
          .receivedAfterBytes = bytesAfterAppend,
          .receivedAtEndOfInput = endOfInput,
          .rgba = *update.snapshot->rgba,
      });
    } else if (update.type == IncrementalUpdateType::AnimationFrameAvailable) {
      require_condition(update.snapshot != nullptr,
                        "animation update must carry a snapshot");
      require_condition(update.snapshot->rgba != nullptr,
                        "animation snapshot must carry pixels");
      require_condition(update.animationFrame != nullptr,
                        "animation update must carry frame metadata");
      trace.animationFrames.push_back(CapturedAnimationFrameUpdate{
          .index = update.animationFrame->index,
          .durationMillis = update.animationFrame->durationMillis,
          .blendOperation = update.animationFrame->blendOperation,
          .disposalOperation = update.animationFrame->disposalOperation,
          .width = update.snapshot->width,
          .height = update.snapshot->height,
          .receivedAfterBytes = bytesAfterAppend,
          .receivedAtEndOfInput = endOfInput,
          .rgba = *update.snapshot->rgba,
      });
    } else if (update.type == IncrementalUpdateType::Complete) {
      if (update.info != nullptr) {
        trace.completionInfo = *update.info;
      }
      ++trace.completeUpdates;
    }
  };

  while (offset < encoded.size()) {
    const size_t count = std::min(chunkSize, encoded.size() - offset);
    bytesAfterAppend = offset + count;
    endOfInput = bytesAfterAppend == encoded.size();
    trace.result =
        decoder->append(encoded.data() + offset, count, endOfInput, sink);
    offset = bytesAfterAppend;
    if (trace.result != IncrementalBackendResult::Accepted) {
      break;
    }
  }
  trace.consumedBytes = offset;
  return trace;
}

void require_condition(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

void require_valid_completed_trace(const IncrementalDecodeTrace& trace,
                                   uint32_t width, uint32_t height) {
  require_condition(trace.result == IncrementalBackendResult::Complete,
                    "decoder must report completion");
  require_condition(trace.completeUpdates == 1,
                    "decoder must publish exactly one completion update");
  require_condition(trace.metadata.has_value(),
                    "decoder must publish metadata");
  require_condition(trace.metadata->width == width &&
                        trace.metadata->height == height,
                    "metadata must preserve source dimensions");
  require_condition(trace.metadata->outputWidth == width &&
                        trace.metadata->outputHeight == height,
                    "full-size options must preserve output dimensions");
  require_condition(!trace.stillUpdates.empty(),
                    "decoder must publish at least one still update");

  uint64_t previousGeneration = 0;
  for (const auto& update : trace.stillUpdates) {
    require_condition(update.width == width && update.height == height,
                      "still dimensions must match metadata");
    require_condition(update.rgba.size() ==
                          static_cast<size_t>(width) * height * 4,
                      "still buffer must contain a complete RGBA canvas");
    require_condition(update.left < update.right && update.top < update.bottom,
                      "dirty region must be non-empty");
    require_condition(update.right <= width && update.bottom <= height,
                      "dirty region must stay within the canvas");
    require_condition(update.generation > previousGeneration,
                      "still generations must increase monotonically");
    previousGeneration = update.generation;
  }
}

size_t populated_pixel_count(const CapturedStillUpdate& update) {
  size_t populated = 0;
  for (size_t offset = 3; offset < update.rgba.size(); offset += 4) {
    if (update.rgba[offset] != 0) {
      ++populated;
    }
  }
  return populated;
}

double mean_absolute_rgb_error(const std::vector<uint8_t>& actual,
                               const std::vector<uint8_t>& expected) {
  require_condition(actual.size() == expected.size(),
                    "pixel buffers must have equal size");
  uint64_t totalError = 0;
  uint64_t samples = 0;
  for (size_t offset = 0; offset < actual.size(); offset += 4) {
    for (size_t channel = 0; channel < 3; ++channel) {
      totalError += static_cast<uint64_t>(
          std::abs(static_cast<int>(actual[offset + channel]) -
                   static_cast<int>(expected[offset + channel])));
      ++samples;
    }
  }
  return static_cast<double>(totalError) / static_cast<double>(samples);
}

uint8_t maximum_channel_error(const std::vector<uint8_t>& actual,
                              const std::vector<uint8_t>& expected) {
  require_condition(actual.size() == expected.size(),
                    "pixel buffers must have equal size");
  int maximumError = 0;
  for (size_t index = 0; index < actual.size(); ++index) {
    maximumError =
        std::max(maximumError, std::abs(static_cast<int>(actual[index]) -
                                        static_cast<int>(expected[index])));
  }
  return static_cast<uint8_t>(maximumError);
}
