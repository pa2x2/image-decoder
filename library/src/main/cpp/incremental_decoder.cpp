#include "incremental_decoder.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace {

constexpr size_t kMaximumSniffBytes = 4096;
constexpr uint64_t kMaximumInputBytes =
    static_cast<uint64_t>(std::numeric_limits<uint32_t>::max());

int32_t capabilities_for(ImageFormat) {
  // Unit 1 establishes lifecycle and format detection. Format backends add
  // their capability flags as they are implemented in later units.
  return 0;
}

} // namespace

IncrementalDecoderSession::IncrementalDecoderSession(
    IncrementalDecodeOptionsNative options)
    : options(std::move(options)) {
  sniffBuffer.reserve(32);
}

IncrementalAppendResult IncrementalDecoderSession::append(const uint8_t* bytes,
                                                          size_t size,
                                                          bool endOfInput) {
  if (inputEnded) {
    return IncrementalAppendResult::InputAlreadyEnded;
  }
  if (size > kMaximumInputBytes - totalInputBytes) {
    terminal = true;
    updates.push_back({IncrementalUpdateType::Error, -1, 0});
    return IncrementalAppendResult::InputTooLarge;
  }

  totalInputBytes += size;
  if (!terminal && sniffBuffer.size() < kMaximumSniffBytes && size > 0) {
    const size_t copySize =
        std::min(size, kMaximumSniffBytes - sniffBuffer.size());
    sniffBuffer.insert(sniffBuffer.end(), bytes, bytes + copySize);
  }

  if (!terminal) {
    detectFormat(endOfInput);
  }
  if (endOfInput) {
    inputEnded = true;
  }
  return IncrementalAppendResult::Accepted;
}

bool IncrementalDecoderSession::pollUpdate(IncrementalUpdate* update) {
  if (updates.empty()) {
    return false;
  }
  *update = updates.front();
  updates.pop_front();
  return true;
}

void IncrementalDecoderSession::detectFormat(bool endOfInput) {
  const auto detection =
      detect_image_format(sniffBuffer.empty() ? nullptr : sniffBuffer.data(),
                          sniffBuffer.size(), endOfInput);
  if (detection.state == ImageFormatDetectionState::NeedMoreData) {
    return;
  }
  if (detection.state == ImageFormatDetectionState::Unsupported) {
    publishUnsupported(-1);
    return;
  }

  const int32_t format = static_cast<int32_t>(detection.format);
  const int32_t capabilities = capabilities_for(detection.format);
  updates.push_back(
      {IncrementalUpdateType::FormatDetected, format, capabilities});

  if (capabilities == 0) {
    publishUnsupported(format);
  }
}

void IncrementalDecoderSession::publishUnsupported(int32_t format) {
  updates.push_back({IncrementalUpdateType::Unsupported, format, 0});
  terminal = true;
}
