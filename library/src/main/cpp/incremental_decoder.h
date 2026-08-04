#ifndef IMAGEDECODER_INCREMENTAL_DECODER_H
#define IMAGEDECODER_INCREMENTAL_DECODER_H

#include "image_format.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

enum class IncrementalUpdateType : int32_t {
  FormatDetected = 1,
  Unsupported = 2,
  Error = 3,
};

enum IncrementalCapability : int32_t {
  IncrementalCapabilityStill = 1,
  IncrementalCapabilityAnimation = 1 << 1,
};

struct IncrementalUpdate {
  IncrementalUpdateType type;
  int32_t format;
  int32_t capabilities;
};

enum class IncrementalAppendResult {
  Accepted,
  InputAlreadyEnded,
  InputTooLarge,
};

struct IncrementalDecodeOptionsNative {
  uint32_t preferredOutputWidth;
  uint64_t maximumBitmapPixels;
  std::vector<uint8_t> displayProfile;
};

class IncrementalDecoderSession {
public:
  explicit IncrementalDecoderSession(IncrementalDecodeOptionsNative options);

  IncrementalAppendResult append(const uint8_t* bytes, size_t size,
                                 bool endOfInput);
  bool pollUpdate(IncrementalUpdate* update);

private:
  void detectFormat(bool endOfInput);
  void publishUnsupported(int32_t format);

  IncrementalDecodeOptionsNative options;
  std::vector<uint8_t> sniffBuffer;
  std::deque<IncrementalUpdate> updates;
  uint64_t totalInputBytes = 0;
  bool inputEnded = false;
  bool terminal = false;
};

#endif // IMAGEDECODER_INCREMENTAL_DECODER_H
