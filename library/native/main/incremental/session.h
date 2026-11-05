#ifndef IMAGEDECODER_INCREMENTAL_SESSION_H
#define IMAGEDECODER_INCREMENTAL_SESSION_H

#include "incremental/format_decoder.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <vector>

enum class IncrementalAppendResult {
  Accepted,
  InputAlreadyEnded,
  InputTooLarge,
};

class IncrementalDecoderSession {
public:
  explicit IncrementalDecoderSession(IncrementalDecodeOptionsNative options);

  IncrementalAppendResult append(const uint8_t* bytes, size_t size,
                                 bool endOfInput);
  bool pollUpdate(IncrementalUpdate* update);

private:
  void detectFormat(bool endOfInput);
  void startDecoder(ImageFormat format, bool endOfInput);
  void appendToDecoder(const uint8_t* bytes, size_t size, bool endOfInput);
  void publishUpdate(IncrementalUpdate&& update);
  void publishUnsupported(int32_t format);
  void publishError(int32_t format);

  IncrementalDecodeOptionsNative options;
  std::vector<uint8_t> pendingInput;
  std::deque<IncrementalUpdate> updates;
  std::unique_ptr<IncrementalFormatDecoder> decoder;
  uint64_t totalInputBytes = 0;
  int32_t detectedFormat = -1;
  bool inputEnded = false;
  bool terminal = false;
};

#endif // IMAGEDECODER_INCREMENTAL_SESSION_H
