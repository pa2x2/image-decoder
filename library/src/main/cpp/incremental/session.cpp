#include "incremental/session.h"

#include "log.h"

#ifdef HAVE_LIBJPEG
#include "incremental/jpeg_decoder.h"
#endif
#ifdef HAVE_LIBJXL
#include "incremental/jxl_decoder.h"
#endif
#ifdef HAVE_LIBPNG
#include "incremental/png_decoder.h"
#endif
#ifdef HAVE_LIBWEBP
#include "incremental/webp_decoder.h"
#endif
#include "incremental/gif_decoder.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace {

constexpr size_t kMaximumSniffBytes = 4096;
constexpr uint64_t kMaximumInputBytes =
    static_cast<uint64_t>(std::numeric_limits<uint32_t>::max());
constexpr size_t kMaximumPendingAnimationFrames = 16;
constexpr size_t kMaximumPendingAnimationBytes = 64 * 1024 * 1024;

int32_t capabilities_for(ImageFormat format) {
  switch (format) {
#ifdef HAVE_LIBJPEG
  case ImageFormat::Jpeg:
    return IncrementalCapabilityStill;
#endif
#ifdef HAVE_LIBJXL
  case ImageFormat::Jxl:
    return IncrementalCapabilityStill | IncrementalCapabilityAnimation;
#endif
#ifdef HAVE_LIBPNG
  case ImageFormat::Png:
    return IncrementalCapabilityStill | IncrementalCapabilityAnimation;
#endif
#ifdef HAVE_LIBWEBP
  case ImageFormat::Webp:
    return IncrementalCapabilityStill | IncrementalCapabilityAnimation;
#endif
  case ImageFormat::Gif:
    return IncrementalCapabilityAnimation;
  default:
    return 0;
  }
}

std::unique_ptr<IncrementalFormatDecoder>
create_decoder(ImageFormat format,
               const IncrementalDecodeOptionsNative& options) {
  switch (format) {
#ifdef HAVE_LIBJPEG
  case ImageFormat::Jpeg:
    return create_incremental_jpeg_decoder(options);
#endif
#ifdef HAVE_LIBJXL
  case ImageFormat::Jxl:
    return create_incremental_jxl_decoder(options);
#endif
#ifdef HAVE_LIBPNG
  case ImageFormat::Png:
    return create_incremental_png_decoder(options);
#endif
#ifdef HAVE_LIBWEBP
  case ImageFormat::Webp:
    return create_incremental_webp_decoder(options);
#endif
  case ImageFormat::Gif:
    return create_incremental_gif_decoder(options);
  default:
    return nullptr;
  }
}

} // namespace

IncrementalDecoderSession::IncrementalDecoderSession(
    IncrementalDecodeOptionsNative options)
    : options(std::move(options)) {
  pendingInput.reserve(32);
}

IncrementalAppendResult IncrementalDecoderSession::append(const uint8_t* bytes,
                                                          size_t size,
                                                          bool endOfInput) {
  if (inputEnded) {
    return IncrementalAppendResult::InputAlreadyEnded;
  }
  if (size > kMaximumInputBytes - totalInputBytes) {
    terminal = true;
    publishError(detectedFormat);
    return IncrementalAppendResult::InputTooLarge;
  }
  totalInputBytes += size;

  if (!terminal) {
    if (decoder != nullptr) {
      appendToDecoder(bytes, size, endOfInput);
    } else {
      if (size > 0) {
        pendingInput.insert(pendingInput.end(), bytes, bytes + size);
      }
      detectFormat(endOfInput);
    }
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
  *update = std::move(updates.front());
  updates.pop_front();
  return true;
}

void IncrementalDecoderSession::detectFormat(bool endOfInput) {
  const size_t sniffSize = std::min(pendingInput.size(), kMaximumSniffBytes);
  const auto detection =
      detect_image_format(pendingInput.empty() ? nullptr : pendingInput.data(),
                          sniffSize, endOfInput);
  if (detection.state == ImageFormatDetectionState::NeedMoreData) {
    return;
  }
  if (detection.state == ImageFormatDetectionState::Unsupported) {
    pendingInput.clear();
    publishUnsupported(-1);
    return;
  }

  detectedFormat = static_cast<int32_t>(detection.format);
  const int32_t capabilities = capabilities_for(detection.format);
  publishUpdate(IncrementalUpdate{
      .type = IncrementalUpdateType::FormatDetected,
      .format = detectedFormat,
      .capabilities = capabilities,
      .info = nullptr,
      .snapshot = nullptr,
      .animationFrame = nullptr,
  });
  if (capabilities == 0) {
    pendingInput.clear();
    publishUnsupported(detectedFormat);
    return;
  }
  startDecoder(detection.format, endOfInput);
}

void IncrementalDecoderSession::startDecoder(ImageFormat format,
                                             bool endOfInput) {
  try {
    decoder = create_decoder(format, options);
    if (decoder == nullptr) {
      pendingInput.clear();
      publishUnsupported(detectedFormat);
      return;
    }
    appendToDecoder(pendingInput.empty() ? nullptr : pendingInput.data(),
                    pendingInput.size(), endOfInput);
    pendingInput.clear();
  } catch (const std::exception& error) {
    LOGW("Failed to start incremental decoder: %s", error.what());
    pendingInput.clear();
    decoder.reset();
    publishError(detectedFormat);
  }
}

void IncrementalDecoderSession::appendToDecoder(const uint8_t* bytes,
                                                size_t size, bool endOfInput) {
  try {
    const auto result = decoder->append(bytes, size, endOfInput,
                                        [this](IncrementalUpdate&& update) {
                                          publishUpdate(std::move(update));
                                        });
    if (terminal) {
      decoder.reset();
      return;
    }
    if (result == IncrementalBackendResult::Unsupported) {
      decoder.reset();
      publishUnsupported(detectedFormat);
    } else if (result == IncrementalBackendResult::Complete) {
      decoder.reset();
      terminal = true;
    }
  } catch (const std::exception& error) {
    LOGW("Incremental decode failed: %s", error.what());
    decoder.reset();
    publishError(detectedFormat);
  }
}

void IncrementalDecoderSession::publishUpdate(IncrementalUpdate&& update) {
  if (terminal) {
    return;
  }
  if (update.type == IncrementalUpdateType::StillImageAvailable) {
    const auto existing =
        std::find_if(updates.begin(), updates.end(), [](const auto& pending) {
          return pending.type == IncrementalUpdateType::StillImageAvailable;
        });
    if (existing != updates.end()) {
      existing->snapshot->left =
          std::min(existing->snapshot->left, update.snapshot->left);
      existing->snapshot->top =
          std::min(existing->snapshot->top, update.snapshot->top);
      existing->snapshot->right =
          std::max(existing->snapshot->right, update.snapshot->right);
      existing->snapshot->bottom =
          std::max(existing->snapshot->bottom, update.snapshot->bottom);
      existing->snapshot->generation = update.snapshot->generation;
      existing->snapshot->rgba = std::move(update.snapshot->rgba);
      return;
    }
  }
  if (update.type == IncrementalUpdateType::AnimationFrameAvailable) {
    size_t pendingFrames = 0;
    size_t pendingBytes = 0;
    for (const auto& pending : updates) {
      if (pending.type == IncrementalUpdateType::AnimationFrameAvailable) {
        ++pendingFrames;
        if (pending.snapshot != nullptr && pending.snapshot->rgba != nullptr) {
          pendingBytes += pending.snapshot->rgba->size();
        }
      }
    }
    const size_t incomingBytes =
        update.snapshot != nullptr && update.snapshot->rgba != nullptr
            ? update.snapshot->rgba->size()
            : 0;
    const bool exceedsByteLimit =
        incomingBytes > kMaximumPendingAnimationBytes ||
        pendingBytes > kMaximumPendingAnimationBytes - incomingBytes;
    if (pendingFrames >= kMaximumPendingAnimationFrames || exceedsByteLimit) {
      updates.erase(std::remove_if(
                        updates.begin(), updates.end(),
                        [](const auto& pending) {
                          return pending.type ==
                                 IncrementalUpdateType::AnimationFrameAvailable;
                        }),
                    updates.end());
      updates.push_back(IncrementalUpdate{
          .type = IncrementalUpdateType::Unsupported,
          .format = detectedFormat,
          .capabilities = 0,
          .info = nullptr,
          .snapshot = nullptr,
          .animationFrame = nullptr,
      });
      terminal = true;
      return;
    }
  }
  updates.push_back(std::move(update));
}

void IncrementalDecoderSession::publishUnsupported(int32_t format) {
  publishUpdate(IncrementalUpdate{
      .type = IncrementalUpdateType::Unsupported,
      .format = format,
      .capabilities = 0,
      .info = nullptr,
      .snapshot = nullptr,
      .animationFrame = nullptr,
  });
  terminal = true;
}

void IncrementalDecoderSession::publishError(int32_t format) {
  publishUpdate(IncrementalUpdate{
      .type = IncrementalUpdateType::Error,
      .format = format,
      .capabilities = 0,
      .info = nullptr,
      .snapshot = nullptr,
      .animationFrame = nullptr,
  });
  terminal = true;
}
