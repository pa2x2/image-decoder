#include "incremental/jxl_decoder.h"

#include "incremental/color_transform.h"
#include "incremental/image_canvas.h"

#include <jxl/decode.h>
#include <jxl/decode_cxx.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

constexpr size_t kMaximumPendingJxlInputBytes = 16 * 1024 * 1024;
constexpr size_t kMaximumJxlProfileBytes = 4 * 1024 * 1024;
constexpr size_t kMinimumJxlFlushInputBytes = 256 * 1024;

class IncrementalJxlDecoder final : public IncrementalFormatDecoder {
public:
  explicit IncrementalJxlDecoder(const IncrementalDecodeOptionsNative& options)
      : options(options), colorTransform(options),
        decoder(JxlDecoderMake(nullptr)) {
    if (decoder == nullptr) {
      throw std::runtime_error("Failed to create incremental JXL decoder");
    }
    const int events = JXL_DEC_BASIC_INFO | JXL_DEC_COLOR_ENCODING |
                       JXL_DEC_FRAME | JXL_DEC_FULL_IMAGE |
                       JXL_DEC_FRAME_PROGRESSION;
    if (JxlDecoderSubscribeEvents(decoder.get(), events) != JXL_DEC_SUCCESS) {
      throw std::runtime_error("Failed to subscribe to incremental JXL events");
    }
    // Keep libjxl's default single-threaded runner because the output callback
    // writes one owned preview canvas. Coalescing makes every displayed
    // animation frame an oriented, fully composed replacement canvas.
    if (JxlDecoderSetCoalescing(decoder.get(), JXL_TRUE) != JXL_DEC_SUCCESS ||
        JxlDecoderSetUnpremultiplyAlpha(decoder.get(), JXL_TRUE) !=
            JXL_DEC_SUCCESS ||
        JxlDecoderSetProgressiveDetail(decoder.get(), kPasses) !=
            JXL_DEC_SUCCESS) {
      throw std::runtime_error("Failed to configure incremental JXL decoder");
    }
  }

  IncrementalBackendResult append(const uint8_t* bytes, size_t size,
                                  bool endOfInput,
                                  const IncrementalUpdateSink& sink) override {
    if (size > kMaximumPendingJxlInputBytes - pendingInput.size()) {
      return IncrementalBackendResult::Unsupported;
    }
    if (size > 0) {
      pendingInput.insert(pendingInput.end(), bytes, bytes + size);
      bytesSinceProgressiveFlush =
          std::min(kMinimumJxlFlushInputBytes,
                   bytesSinceProgressiveFlush +
                       std::min(size, kMinimumJxlFlushInputBytes));
    }
    if (JxlDecoderSetInput(decoder.get(),
                           pendingInput.empty() ? nullptr : pendingInput.data(),
                           pendingInput.size()) != JXL_DEC_SUCCESS) {
      throw std::runtime_error("Failed to append incremental JXL input");
    }
    if (endOfInput) {
      JxlDecoderCloseInput(decoder.get());
    }

    while (true) {
      const JxlDecoderStatus status = JxlDecoderProcessInput(decoder.get());
      switch (status) {
      case JXL_DEC_ERROR:
        throw std::runtime_error("Incremental JXL decoder rejected input");
      case JXL_DEC_NEED_MORE_INPUT:
        publishProgressiveFirstFrame(sink, false);
        releaseInput();
        if (endOfInput) {
          throw std::runtime_error("Truncated JXL input");
        }
        return IncrementalBackendResult::Accepted;
      case JXL_DEC_BASIC_INFO:
        readBasicInfo(sink);
        if (resourceUnsupported) {
          return IncrementalBackendResult::Unsupported;
        }
        break;
      case JXL_DEC_COLOR_ENCODING:
        configureColorTransform();
        break;
      case JXL_DEC_FRAME:
        beginFrame();
        break;
      case JXL_DEC_NEED_IMAGE_OUT_BUFFER:
        configureFrameOutput();
        break;
      case JXL_DEC_FRAME_PROGRESSION:
        publishProgressiveFirstFrame(sink, true);
        break;
      case JXL_DEC_FULL_IMAGE:
        publishCompleteFrame(sink);
        break;
      case JXL_DEC_SUCCESS:
        if (!receivedFullImage) {
          throw std::runtime_error("JXL input completed without image pixels");
        }
        sink(makeInfoUpdate(IncrementalUpdateType::Complete));
        return IncrementalBackendResult::Complete;
      default:
        throw std::runtime_error("Unexpected incremental JXL decoder status");
      }
      if (callbackFailed) {
        throw std::runtime_error("Incremental JXL pixel callback failed");
      }
    }
  }

private:
  static void receivePixels(void* opaque, size_t x, size_t y, size_t pixelCount,
                            const void* pixels) {
    static_cast<IncrementalJxlDecoder*>(opaque)->receivePixels(
        x, y, pixelCount, static_cast<const uint8_t*>(pixels));
  }

  void receivePixels(size_t x, size_t y, size_t pixelCount,
                     const uint8_t* pixels) {
    if (callbackFailed || canvas == nullptr || !colorConfigured ||
        pixels == nullptr || y >= basicInfo.ysize || x > basicInfo.xsize ||
        pixelCount > basicInfo.xsize - x) {
      callbackFailed = true;
      return;
    }
    uint32_t outputY;
    if (!canvas->outputRowForSource(static_cast<uint32_t>(y), &outputY)) {
      return;
    }
    const auto first = std::lower_bound(sampledSourceColumns.begin(),
                                        sampledSourceColumns.end(), x);
    const auto last =
        std::lower_bound(sampledSourceColumns.begin(),
                         sampledSourceColumns.end(), x + pixelCount);
    const size_t sampledCount = static_cast<size_t>(last - first);
    if (sampledCount == 0) {
      return;
    }
    const size_t firstOutputX =
        static_cast<size_t>(first - sampledSourceColumns.begin());
    for (size_t sampled = 0; sampled < sampledCount; ++sampled) {
      const size_t sourceOffset =
          (sampledSourceColumns[firstOutputX + sampled] - x) *
          pixelFormat.num_channels;
      std::memcpy(sampledInputPixels.data() +
                      sampled * pixelFormat.num_channels,
                  pixels + sourceOffset, pixelFormat.num_channels);
    }
    colorTransform.apply(sampledInputPixels.data(), transformedPixels.data(),
                         static_cast<uint32_t>(sampledCount));
    for (size_t sampled = 0; sampled < sampledCount; ++sampled) {
      canvas->updatePixel(static_cast<uint32_t>(firstOutputX + sampled),
                          outputY, transformedPixels.data() + sampled * 4,
                          true);
    }
  }

  void readBasicInfo(const IncrementalUpdateSink& sink) {
    if (JxlDecoderGetBasicInfo(decoder.get(), &basicInfo) != JXL_DEC_SUCCESS) {
      throw std::runtime_error("Failed to read incremental JXL basic info");
    }
    if (basicInfo.have_animation &&
        (basicInfo.animation.tps_numerator == 0 ||
         basicInfo.animation.tps_denominator == 0 ||
         basicInfo.animation.num_loops >
             static_cast<uint32_t>(std::numeric_limits<int32_t>::max()))) {
      resourceUnsupported = true;
      return;
    }
    if (basicInfo.num_color_channels != 1 &&
        basicInfo.num_color_channels != 3) {
      resourceUnsupported = true;
      return;
    }
    const auto output = calculate_incremental_output_dimensions(
        basicInfo.xsize, basicInfo.ysize, options);
    canvas = std::make_unique<IncrementalImageCanvas>(basicInfo.xsize,
                                                      basicInfo.ysize, output);
    hasAlpha = basicInfo.alpha_bits > 0;
    pixelFormat = JxlPixelFormat{
        static_cast<uint32_t>(basicInfo.num_color_channels +
                              (hasAlpha ? 1 : 0)),
        JXL_TYPE_UINT8,
        JXL_NATIVE_ENDIAN,
        0,
    };
    if (pixelFormat.num_channels == 0 || pixelFormat.num_channels > 4) {
      resourceUnsupported = true;
      return;
    }
    sampledSourceColumns.resize(canvas->outputWidth());
    for (uint32_t outputX = 0; outputX < canvas->outputWidth(); ++outputX) {
      sampledSourceColumns[outputX] = canvas->sourceXForOutput(outputX);
    }
    sampledInputPixels.resize(static_cast<size_t>(canvas->outputWidth()) *
                              pixelFormat.num_channels);
    transformedPixels.resize(static_cast<size_t>(canvas->outputWidth()) * 4);
    sink(makeInfoUpdate(IncrementalUpdateType::MetadataAvailable));
  }

  void configureColorTransform() {
    size_t profileSize = 0;
    if (JxlDecoderGetICCProfileSize(decoder.get(),
                                    JXL_COLOR_PROFILE_TARGET_DATA,
                                    &profileSize) != JXL_DEC_SUCCESS ||
        profileSize == 0 || profileSize > kMaximumJxlProfileBytes) {
      throw std::runtime_error("Incremental JXL color profile is unavailable");
    }
    std::vector<uint8_t> profileBytes(profileSize);
    if (JxlDecoderGetColorAsICCProfile(
            decoder.get(), JXL_COLOR_PROFILE_TARGET_DATA, profileBytes.data(),
            profileBytes.size()) != JXL_DEC_SUCCESS) {
      throw std::runtime_error("Failed to read incremental JXL color profile");
    }
    cmsHPROFILE sourceProfile =
        cmsOpenProfileFromMem(profileBytes.data(), profileBytes.size());
    if (sourceProfile == nullptr) {
      throw std::runtime_error("Incremental JXL color profile is invalid");
    }
    const bool grayscale = basicInfo.num_color_channels == 1;
    const auto colorSpace = cmsGetColorSpace(sourceProfile);
    if ((grayscale && colorSpace != cmsSigGrayData) ||
        (!grayscale && colorSpace != cmsSigRgbData)) {
      cmsCloseProfile(sourceProfile);
      throw std::runtime_error("Incremental JXL color profile has wrong space");
    }
    const cmsUInt32Number inputType =
        grayscale ? (hasAlpha ? TYPE_GRAYA_8 : TYPE_GRAY_8)
                  : (hasAlpha ? TYPE_RGBA_8 : TYPE_RGB_8);
    colorTransform.configure(sourceProfile, inputType, hasAlpha, !hasAlpha);
    colorConfigured = true;
  }

  void beginFrame() {
    if (JxlDecoderGetFrameHeader(decoder.get(), &frameHeader) !=
        JXL_DEC_SUCCESS) {
      throw std::runtime_error("Failed to read incremental JXL frame header");
    }
    frameDurationMillis =
        basicInfo.have_animation ? durationMillis(frameHeader.duration) : 0;
    frameActive = true;
    outputConfigured = false;
    callbackFailed = false;
  }

  void configureFrameOutput() {
    if (!frameActive || !colorConfigured || canvas == nullptr) {
      throw std::runtime_error("Incremental JXL frame output is not ready");
    }
    if (JxlDecoderSetImageOutCallback(decoder.get(), &pixelFormat,
                                      &IncrementalJxlDecoder::receivePixels,
                                      this) != JXL_DEC_SUCCESS) {
      throw std::runtime_error("Failed to configure incremental JXL output");
    }
    outputConfigured = true;
  }

  void publishProgressiveFirstFrame(const IncrementalUpdateSink& sink,
                                    bool meaningfulProgressEvent) {
    if (!outputConfigured || publishedFrameCount != 0 ||
        (!meaningfulProgressEvent &&
         bytesSinceProgressiveFlush < kMinimumJxlFlushInputBytes) ||
        JxlDecoderFlushImage(decoder.get()) != JXL_DEC_SUCCESS) {
      return;
    }
    if (callbackFailed) {
      throw std::runtime_error("Incremental JXL progressive flush failed");
    }
    auto snapshot = canvas->takeSnapshot();
    if (snapshot != nullptr) {
      sink(makePixelUpdate(IncrementalUpdateType::StillImageAvailable,
                           std::move(snapshot)));
    }
    bytesSinceProgressiveFlush = 0;
  }

  void publishCompleteFrame(const IncrementalUpdateSink& sink) {
    if (!frameActive || !outputConfigured || canvas == nullptr ||
        callbackFailed) {
      throw std::runtime_error("Incremental JXL frame did not produce pixels");
    }
    receivedFullImage = true;
    if (basicInfo.have_animation) {
      auto update =
          makePixelUpdate(IncrementalUpdateType::AnimationFrameAvailable,
                          canvas->takeFullSnapshot());
      update.animationFrame = std::make_unique<IncrementalAnimationFrameNative>(
          IncrementalAnimationFrameNative{
              .index = publishedFrameCount,
              .durationMillis = frameDurationMillis,
              .blendOperation = IncrementalBlendOperationNative::Source,
              .disposalOperation = IncrementalDisposalOperationNative::None,
          });
      sink(std::move(update));
      ++publishedFrameCount;
    } else {
      sink(makePixelUpdate(IncrementalUpdateType::StillImageAvailable,
                           canvas->takeFullSnapshot()));
    }
    frameActive = false;
    outputConfigured = false;
  }

  uint64_t durationMillis(uint32_t durationTicks) const {
    const long double milliseconds =
        static_cast<long double>(durationTicks) * 1000 *
        basicInfo.animation.tps_denominator / basicInfo.animation.tps_numerator;
    const long double maximum =
        static_cast<long double>(std::numeric_limits<int64_t>::max());
    return static_cast<uint64_t>(
        std::floor(std::min(milliseconds, maximum) + 0.5L));
  }

  void releaseInput() {
    const size_t remaining = JxlDecoderReleaseInput(decoder.get());
    if (remaining > pendingInput.size()) {
      throw std::runtime_error("Incremental JXL released invalid input size");
    }
    if (remaining > 0) {
      std::memmove(pendingInput.data(),
                   pendingInput.data() + pendingInput.size() - remaining,
                   remaining);
    }
    pendingInput.resize(remaining);
  }

  IncrementalUpdate
  makePixelUpdate(IncrementalUpdateType type,
                  std::unique_ptr<IncrementalPixelSnapshot> snapshot) const {
    return IncrementalUpdate{
        .type = type,
        .format = static_cast<int32_t>(ImageFormat::Jxl),
        .capabilities = 0,
        .info = nullptr,
        .snapshot = std::move(snapshot),
        .animationFrame = nullptr,
    };
  }

  IncrementalUpdate makeInfoUpdate(IncrementalUpdateType type) const {
    const bool animated = basicInfo.have_animation != 0;
    IncrementalUpdate update{
        .type = type,
        .format = static_cast<int32_t>(ImageFormat::Jxl),
        .capabilities = 0,
        .info = nullptr,
        .snapshot = nullptr,
        .animationFrame = nullptr,
    };
    update.info =
        std::make_unique<IncrementalImageInfoNative>(IncrementalImageInfoNative{
            .format = ImageFormat::Jxl,
            .width = basicInfo.xsize,
            .height = basicInfo.ysize,
            .outputWidth = canvas->outputWidth(),
            .outputHeight = canvas->outputHeight(),
            .isAnimated = animated,
            .hasAlpha = hasAlpha,
            .frameCount = animated && type == IncrementalUpdateType::Complete
                              ? publishedFrameCount
                              : -1,
            .loopCount =
                animated ? static_cast<int32_t>(basicInfo.animation.num_loops)
                         : -1,
        });
    return update;
  }

  IncrementalDecodeOptionsNative options;
  IncrementalColorTransform colorTransform;
  JxlDecoderPtr decoder;
  std::vector<uint8_t> pendingInput;
  std::vector<uint8_t> sampledInputPixels;
  std::vector<uint8_t> transformedPixels;
  std::vector<uint32_t> sampledSourceColumns;
  std::unique_ptr<IncrementalImageCanvas> canvas;
  JxlBasicInfo basicInfo{};
  JxlFrameHeader frameHeader{};
  JxlPixelFormat pixelFormat{};
  uint64_t frameDurationMillis = 0;
  size_t bytesSinceProgressiveFlush = 0;
  int32_t publishedFrameCount = 0;
  bool hasAlpha = false;
  bool colorConfigured = false;
  bool frameActive = false;
  bool outputConfigured = false;
  bool callbackFailed = false;
  bool receivedFullImage = false;
  bool resourceUnsupported = false;
};

} // namespace

std::unique_ptr<IncrementalFormatDecoder>
create_incremental_jxl_decoder(const IncrementalDecodeOptionsNative& options) {
  return std::make_unique<IncrementalJxlDecoder>(options);
}
