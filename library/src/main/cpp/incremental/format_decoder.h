#ifndef IMAGEDECODER_INCREMENTAL_FORMAT_DECODER_H
#define IMAGEDECODER_INCREMENTAL_FORMAT_DECODER_H

#include "image_format.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

constexpr uint32_t kMaximumIncrementalOutputDimension = 32768;
constexpr uint32_t kMaximumIncrementalSourceDimension = 1000000;

enum class IncrementalUpdateType : int32_t {
  FormatDetected = 1,
  Unsupported = 2,
  Error = 3,
  MetadataAvailable = 4,
  StillImageAvailable = 5,
  Complete = 6,
};

enum IncrementalCapability : int32_t {
  IncrementalCapabilityStill = 1,
  IncrementalCapabilityAnimation = 1 << 1,
};

struct IncrementalImageInfoNative {
  ImageFormat format;
  uint32_t width;
  uint32_t height;
  uint32_t outputWidth;
  uint32_t outputHeight;
  bool isAnimated;
  bool hasAlpha;
  int32_t frameCount = -1;
  int32_t loopCount = -1;
};

struct IncrementalPixelSnapshot {
  uint32_t width;
  uint32_t height;
  uint32_t left;
  uint32_t top;
  uint32_t right;
  uint32_t bottom;
  uint64_t generation;
  std::shared_ptr<std::vector<uint8_t>> rgba;
};

struct IncrementalUpdate {
  IncrementalUpdateType type;
  int32_t format = -1;
  int32_t capabilities = 0;
  std::unique_ptr<IncrementalImageInfoNative> info;
  std::unique_ptr<IncrementalPixelSnapshot> snapshot;
};

struct IncrementalDecodeOptionsNative {
  uint32_t preferredOutputWidth;
  uint64_t maximumBitmapPixels;
  std::vector<uint8_t> displayProfile;
};

enum class IncrementalBackendResult {
  Accepted,
  Complete,
  Unsupported,
};

using IncrementalUpdateSink = std::function<void(IncrementalUpdate&&)>;

class IncrementalFormatDecoder {
public:
  virtual ~IncrementalFormatDecoder() = default;

  virtual IncrementalBackendResult
  append(const uint8_t* bytes, size_t size, bool endOfInput,
         const IncrementalUpdateSink& sink) = 0;
};

#endif // IMAGEDECODER_INCREMENTAL_FORMAT_DECODER_H
