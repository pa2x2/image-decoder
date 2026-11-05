#include "incremental/animated_webp_decoder.h"

#include "incremental/animation_canvas.h"
#include "incremental/color_transform.h"

#include <src/webp/decode.h>
#include <src/webp/demux.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

constexpr size_t kMaximumAnimatedWebpInputBytes = 256 * 1024 * 1024;

class IncrementalAnimatedWebpDecoder final : public IncrementalFormatDecoder {
public:
  explicit IncrementalAnimatedWebpDecoder(
      const IncrementalDecodeOptionsNative& options)
      : options(options), colorTransform(options) {}

  IncrementalBackendResult append(const uint8_t* bytes, size_t size,
                                  bool endOfInput,
                                  const IncrementalUpdateSink& sink) override {
    if (size > kMaximumAnimatedWebpInputBytes - encodedInput.size()) {
      return IncrementalBackendResult::Unsupported;
    }
    if (size > 0) {
      encodedInput.insert(encodedInput.end(), bytes, bytes + size);
    }

    WebPData data{encodedInput.data(), encodedInput.size()};
    WebPDemuxState state = WEBP_DEMUX_PARSING_HEADER;
    auto demux = std::unique_ptr<WebPDemuxer, decltype(&WebPDemuxDelete)>(
        WebPDemuxPartial(&data, &state), WebPDemuxDelete);
    if (demux == nullptr) {
      if (state == WEBP_DEMUX_PARSE_ERROR) {
        throw std::runtime_error("Failed to parse animated WebP container");
      }
      if (endOfInput) {
        throw std::runtime_error("Truncated animated WebP header");
      }
      return IncrementalBackendResult::Accepted;
    }

    if (state >= WEBP_DEMUX_PARSED_HEADER && canvas == nullptr) {
      initialize(demux.get(), sink);
      if (resourceUnsupported) {
        return IncrementalBackendResult::Unsupported;
      }
    }
    if (canvas != nullptr) {
      publishCompleteFrames(demux.get(), sink);
    }

    if (state == WEBP_DEMUX_DONE) {
      if (canvas == nullptr || publishedFrameCount == 0) {
        throw std::runtime_error("Animated WebP contains no playable frames");
      }
      sink(makeInfoUpdate(IncrementalUpdateType::Complete,
                          WebPDemuxGetI(demux.get(), WEBP_FF_FRAME_COUNT)));
      return IncrementalBackendResult::Complete;
    }
    if (endOfInput) {
      throw std::runtime_error("Truncated animated WebP input");
    }
    return IncrementalBackendResult::Accepted;
  }

private:
  bool initialize(const WebPDemuxer* demux, const IncrementalUpdateSink& sink) {
    const uint32_t flags = WebPDemuxGetI(demux, WEBP_FF_FORMAT_FLAGS);
    if ((flags & ANIMATION_FLAG) == 0) {
      throw std::runtime_error("Expected an animated WebP container");
    }
    WebPIterator firstFrame{};
    if (!WebPDemuxGetFrame(demux, 1, &firstFrame)) {
      return false;
    }
    WebPDemuxReleaseIterator(&firstFrame);
    sourceWidth = WebPDemuxGetI(demux, WEBP_FF_CANVAS_WIDTH);
    sourceHeight = WebPDemuxGetI(demux, WEBP_FF_CANVAS_HEIGHT);
    loopCount = WebPDemuxGetI(demux, WEBP_FF_LOOP_COUNT);
    hasAlpha = (flags & ALPHA_FLAG) != 0;
    if (!IncrementalAnimationCanvas::fits(sourceWidth, sourceHeight, options)) {
      resourceUnsupported = true;
      return false;
    }

    const uint32_t background = WebPDemuxGetI(demux, WEBP_FF_BACKGROUND_COLOR);
    const std::array<uint8_t, 4> backgroundRgba = {
        static_cast<uint8_t>((background >> 16) & 0xFF),
        static_cast<uint8_t>((background >> 8) & 0xFF),
        static_cast<uint8_t>(background & 0xFF),
        static_cast<uint8_t>((background >> 24) & 0xFF),
    };
    cmsHPROFILE sourceProfile = nullptr;
    if ((flags & ICCP_FLAG) != 0) {
      WebPChunkIterator profile{};
      if (!WebPDemuxGetChunk(demux, "ICCP", 1, &profile)) {
        return false;
      }
      sourceProfile =
          cmsOpenProfileFromMem(profile.chunk.bytes, profile.chunk.size);
      WebPDemuxReleaseChunkIterator(&profile);
      if (sourceProfile != nullptr &&
          cmsGetColorSpace(sourceProfile) != cmsSigRgbData) {
        cmsCloseProfile(sourceProfile);
        sourceProfile = nullptr;
      }
    }
    if (sourceProfile == nullptr) {
      sourceProfile = cmsCreate_sRGBProfile();
    }
    colorTransform.configure(sourceProfile, TYPE_RGBA_8, true, false);
    std::array<uint8_t, 4> transformedBackground{};
    colorTransform.apply(backgroundRgba.data(), transformedBackground.data(),
                         1);
    canvas = std::make_unique<IncrementalAnimationCanvas>(
        sourceWidth, sourceHeight, options, transformedBackground);
    sink(makeInfoUpdate(IncrementalUpdateType::MetadataAvailable, -1));
    return true;
  }

  void publishCompleteFrames(const WebPDemuxer* demux,
                             const IncrementalUpdateSink& sink) {
    while (true) {
      WebPIterator frame{};
      if (!WebPDemuxGetFrame(demux, publishedFrameCount + 1, &frame)) {
        return;
      }
      const bool complete = frame.complete != 0;
      if (complete) {
        publishFrame(frame, sink);
      }
      WebPDemuxReleaseIterator(&frame);
      if (!complete) {
        return;
      }
      ++publishedFrameCount;
    }
  }

  void publishFrame(const WebPIterator& frame,
                    const IncrementalUpdateSink& sink) {
    const auto region = canvas->mapRegion(static_cast<uint32_t>(frame.x_offset),
                                          static_cast<uint32_t>(frame.y_offset),
                                          static_cast<uint32_t>(frame.width),
                                          static_cast<uint32_t>(frame.height));
    std::vector<uint8_t> decodedPixels(static_cast<size_t>(region.sourceWidth) *
                                       region.sourceHeight * 4);

    WebPDecoderConfig config{};
    if (!WebPInitDecoderConfig(&config)) {
      throw std::runtime_error("Failed to initialize animated WebP decoder");
    }
    config.output.colorspace = MODE_RGBA;
    config.output.u.RGBA.rgba = decodedPixels.data();
    config.output.u.RGBA.stride = region.sourceWidth * 4;
    config.output.u.RGBA.size = decodedPixels.size();
    config.output.is_external_memory = 1;
    const VP8StatusCode status =
        WebPDecode(frame.fragment.bytes, frame.fragment.size, &config);
    WebPFreeDecBuffer(&config.output);
    if (status != VP8_STATUS_OK) {
      throw std::runtime_error("Failed to decode animated WebP frame");
    }

    std::vector<uint8_t> transformedRow(
        static_cast<size_t>(region.sourceWidth) * 4);
    for (uint32_t row = 0; row < region.sourceHeight; ++row) {
      uint8_t* pixels = decodedPixels.data() +
                        static_cast<size_t>(row) * region.sourceWidth * 4;
      colorTransform.apply(pixels, transformedRow.data(), region.sourceWidth);
      std::memcpy(pixels, transformedRow.data(), transformedRow.size());
    }

    const auto blend = frame.blend_method == WEBP_MUX_NO_BLEND
                           ? IncrementalBlendOperationNative::Source
                           : IncrementalBlendOperationNative::Over;
    const auto disposal = frame.dispose_method == WEBP_MUX_DISPOSE_BACKGROUND
                              ? IncrementalDisposalOperationNative::Background
                              : IncrementalDisposalOperationNative::None;
    canvas->beginFrame(region, disposal);
    canvas->composite(region, decodedPixels.data(), blend);
    auto update =
        canvas->makeFrameUpdate(ImageFormat::Webp, publishedFrameCount,
                                frame.duration, region, blend, disposal);
    sink(std::move(*update));
    canvas->disposeFrame(region, disposal);
  }

  IncrementalUpdate makeInfoUpdate(IncrementalUpdateType type,
                                   int32_t frameCount) const {
    const auto& output = canvas->outputDimensions();
    IncrementalUpdate update{
        .type = type,
        .format = static_cast<int32_t>(ImageFormat::Webp),
        .capabilities = 0,
        .info = nullptr,
        .snapshot = nullptr,
        .animationFrame = nullptr,
    };
    update.info =
        std::make_unique<IncrementalImageInfoNative>(IncrementalImageInfoNative{
            .format = ImageFormat::Webp,
            .width = sourceWidth,
            .height = sourceHeight,
            .outputWidth = output.width,
            .outputHeight = output.height,
            .isAnimated = true,
            .hasAlpha = hasAlpha,
            .frameCount = frameCount,
            .loopCount = loopCount,
        });
    return update;
  }

  IncrementalDecodeOptionsNative options;
  IncrementalColorTransform colorTransform;
  std::vector<uint8_t> encodedInput;
  std::unique_ptr<IncrementalAnimationCanvas> canvas;
  uint32_t sourceWidth = 0;
  uint32_t sourceHeight = 0;
  int32_t loopCount = -1;
  int32_t publishedFrameCount = 0;
  bool hasAlpha = false;
  bool resourceUnsupported = false;
};

} // namespace

std::unique_ptr<IncrementalFormatDecoder>
create_incremental_animated_webp_decoder(
    const IncrementalDecodeOptionsNative& options) {
  return std::make_unique<IncrementalAnimatedWebpDecoder>(options);
}
