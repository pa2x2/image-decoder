#ifndef IMAGEDECODER_INCREMENTAL_TEST_DECODE_HARNESS_H
#define IMAGEDECODER_INCREMENTAL_TEST_DECODE_HARNESS_H

#include "incremental/format_decoder.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

using IncrementalDecoderFactory =
    std::function<std::unique_ptr<IncrementalFormatDecoder>(
        const IncrementalDecodeOptionsNative&)>;

struct CapturedStillUpdate {
  uint32_t width;
  uint32_t height;
  uint32_t left;
  uint32_t top;
  uint32_t right;
  uint32_t bottom;
  uint64_t generation;
  size_t receivedAfterBytes;
  bool receivedAtEndOfInput;
  std::vector<uint8_t> rgba;
};

struct CapturedAnimationFrameUpdate {
  int32_t index;
  uint64_t durationMillis;
  IncrementalBlendOperationNative blendOperation;
  IncrementalDisposalOperationNative disposalOperation;
  uint32_t width;
  uint32_t height;
  size_t receivedAfterBytes;
  bool receivedAtEndOfInput;
  std::vector<uint8_t> rgba;
};

struct IncrementalDecodeTrace {
  std::optional<IncrementalImageInfoNative> metadata;
  std::optional<IncrementalImageInfoNative> completionInfo;
  std::vector<CapturedStillUpdate> stillUpdates;
  std::vector<CapturedAnimationFrameUpdate> animationFrames;
  IncrementalBackendResult result = IncrementalBackendResult::Accepted;
  size_t completeUpdates = 0;
  size_t consumedBytes = 0;
};

IncrementalDecodeOptionsNative full_size_options(uint32_t width,
                                                 uint32_t height);
// Narrows the output to `outputWidth` while leaving the pixel budget at the
// source size, so only the width limit scales the image.
IncrementalDecodeOptionsNative scaled_options(uint32_t width, uint32_t height,
                                              uint32_t outputWidth);

IncrementalDecodeTrace
decode_in_chunks(const IncrementalDecoderFactory& factory,
                 const IncrementalDecodeOptionsNative& options,
                 const std::vector<uint8_t>& encoded, size_t chunkSize);

void require_condition(bool condition, std::string_view message);
void require_valid_completed_trace(const IncrementalDecodeTrace& trace,
                                   uint32_t width, uint32_t height);
size_t populated_pixel_count(const CapturedStillUpdate& update);
double mean_absolute_rgb_error(const std::vector<uint8_t>& actual,
                               const std::vector<uint8_t>& expected);
uint8_t maximum_channel_error(const std::vector<uint8_t>& actual,
                              const std::vector<uint8_t>& expected);

#endif // IMAGEDECODER_INCREMENTAL_TEST_DECODE_HARNESS_H
