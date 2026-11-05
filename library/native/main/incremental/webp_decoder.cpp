#include "incremental/webp_decoder.h"

#include "incremental/animated_webp_decoder.h"
#include "incremental/color_transform.h"
#include "incremental/image_canvas.h"

#include <src/webp/decode.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

constexpr size_t kMaximumWebpHeaderBytes = 4 * 1024 * 1024 + 64 * 1024;
constexpr size_t kMaximumWebpProfileBytes = 4 * 1024 * 1024;

uint32_t read_little_endian_u32(const uint8_t* bytes) {
  return static_cast<uint32_t>(bytes[0]) |
         static_cast<uint32_t>(bytes[1]) << 8 |
         static_cast<uint32_t>(bytes[2]) << 16 |
         static_cast<uint32_t>(bytes[3]) << 24;
}

class IncrementalStaticWebpDecoder final : public IncrementalFormatDecoder {
public:
  explicit IncrementalStaticWebpDecoder(
      const IncrementalDecodeOptionsNative& options)
      : options(options), colorTransform(options) {
    if (!WebPInitDecoderConfig(&config)) {
      throw std::runtime_error("Failed to initialize incremental WebP config");
    }
  }

  ~IncrementalStaticWebpDecoder() override {
    if (decoder != nullptr) {
      WebPIDelete(decoder);
    }
    WebPFreeDecBuffer(&config.output);
  }

  IncrementalBackendResult append(const uint8_t* bytes, size_t size,
                                  bool endOfInput,
                                  const IncrementalUpdateSink& sink) override {
    if (decoder == nullptr) {
      if (pendingInput.size() + size > kMaximumWebpHeaderBytes) {
        return IncrementalBackendResult::Unsupported;
      }
      if (size > 0) {
        pendingInput.insert(pendingInput.end(), bytes, bytes + size);
      }
      const HeaderState headerState = inspectHeader();
      if (headerState == HeaderState::Unsupported) {
        return IncrementalBackendResult::Unsupported;
      }
      if (headerState == HeaderState::NeedMoreData) {
        if (endOfInput) {
          throw std::runtime_error("Truncated WebP header");
        }
        return IncrementalBackendResult::Accepted;
      }
      initialize(sink);
      lastStatus =
          WebPIAppend(decoder, pendingInput.data(), pendingInput.size());
      pendingInput.clear();
      handleStatus(lastStatus, endOfInput);
    } else {
      if (size > 0) {
        lastStatus = WebPIAppend(decoder, bytes, size);
      }
      handleStatus(lastStatus, endOfInput);
    }

    publishRows(sink);
    if (complete) {
      sink(makeInfoUpdate(IncrementalUpdateType::Complete));
      return IncrementalBackendResult::Complete;
    }
    return IncrementalBackendResult::Accepted;
  }

private:
  enum class HeaderState {
    NeedMoreData,
    Ready,
    Unsupported,
  };

  HeaderState inspectHeader() {
    if (pendingInput.size() < 12) {
      return HeaderState::NeedMoreData;
    }
    size_t offset = 12;
    bool imageChunkFound = false;
    while (offset + 8 <= pendingInput.size()) {
      const uint8_t* chunk = pendingInput.data() + offset;
      const uint32_t chunkSize = read_little_endian_u32(chunk + 4);
      const uint64_t chunkEnd =
          static_cast<uint64_t>(offset) + 8 + chunkSize + (chunkSize & 1U);
      const bool imageChunk = std::memcmp(chunk, "VP8 ", 4) == 0 ||
                              std::memcmp(chunk, "VP8L", 4) == 0;
      if (imageChunk) {
        imageChunkFound = true;
        break;
      }
      if (std::memcmp(chunk, "ANIM", 4) == 0 ||
          std::memcmp(chunk, "ANMF", 4) == 0) {
        return HeaderState::Unsupported;
      }
      if (chunkEnd > pendingInput.size()) {
        return HeaderState::NeedMoreData;
      }
      if (std::memcmp(chunk, "ICCP", 4) == 0) {
        if (chunkSize > kMaximumWebpProfileBytes) {
          return HeaderState::Unsupported;
        }
        profileBytes.assign(chunk + 8, chunk + 8 + chunkSize);
      }
      offset = static_cast<size_t>(chunkEnd);
    }
    if (!imageChunkFound) {
      return HeaderState::NeedMoreData;
    }

    const VP8StatusCode featureStatus = WebPGetFeatures(
        pendingInput.data(), pendingInput.size(), &config.input);
    if (featureStatus == VP8_STATUS_NOT_ENOUGH_DATA) {
      return HeaderState::NeedMoreData;
    }
    if (featureStatus != VP8_STATUS_OK) {
      throw std::runtime_error("Failed to parse incremental WebP features");
    }
    return config.input.has_animation ? HeaderState::Unsupported
                                      : HeaderState::Ready;
  }

  void initialize(const IncrementalUpdateSink& sink) {
    outputDimensions = calculate_incremental_output_dimensions(
        config.input.width, config.input.height, options);
    decodedPixels.resize(static_cast<size_t>(outputDimensions.width) *
                         outputDimensions.height * 4);
    transformedRow.resize(static_cast<size_t>(outputDimensions.width) * 4);

    config.options.use_scaling =
        outputDimensions.width != static_cast<uint32_t>(config.input.width) ||
        outputDimensions.height != static_cast<uint32_t>(config.input.height);
    config.options.scaled_width = outputDimensions.width;
    config.options.scaled_height = outputDimensions.height;
    config.output.colorspace = MODE_RGBA;
    config.output.u.RGBA.rgba = decodedPixels.data();
    config.output.u.RGBA.stride = outputDimensions.width * 4;
    config.output.u.RGBA.size = decodedPixels.size();
    config.output.is_external_memory = 1;

    cmsHPROFILE sourceProfile = nullptr;
    if (!profileBytes.empty()) {
      sourceProfile =
          cmsOpenProfileFromMem(profileBytes.data(), profileBytes.size());
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

    canvas = std::make_unique<IncrementalImageCanvas>(
        outputDimensions.width, outputDimensions.height, outputDimensions);
    decoder = WebPIDecode(nullptr, 0, &config);
    if (decoder == nullptr) {
      throw std::runtime_error("Failed to create incremental WebP decoder");
    }
    sink(makeInfoUpdate(IncrementalUpdateType::MetadataAvailable));
  }

  void handleStatus(VP8StatusCode status, bool endOfInput) {
    if (status == VP8_STATUS_OK) {
      complete = true;
      return;
    }
    if (status == VP8_STATUS_SUSPENDED && !endOfInput) {
      return;
    }
    if (status == VP8_STATUS_SUSPENDED) {
      throw std::runtime_error("Truncated WebP input");
    }
    throw std::runtime_error("Incremental WebP decode failed");
  }

  void publishRows(const IncrementalUpdateSink& sink) {
    int decodedRowCount = 0;
    int width = 0;
    int height = 0;
    int stride = 0;
    uint8_t* pixels =
        WebPIDecGetRGB(decoder, &decodedRowCount, &width, &height, &stride);
    if (pixels == nullptr || decodedRowCount <= publishedRows) {
      return;
    }
    if (width != static_cast<int>(outputDimensions.width) ||
        height != static_cast<int>(outputDimensions.height) ||
        stride < width * 4 || decodedRowCount > height) {
      throw std::runtime_error("Unexpected incremental WebP output bounds");
    }
    for (int row = publishedRows; row < decodedRowCount; ++row) {
      colorTransform.apply(pixels + static_cast<size_t>(row) * stride,
                           transformedRow.data(), outputDimensions.width);
      canvas->updateSourceRow(row, transformedRow.data());
    }
    publishedRows = decodedRowCount;

    auto snapshot = canvas->takeSnapshot();
    if (snapshot == nullptr) {
      return;
    }
    IncrementalUpdate update{
        .type = IncrementalUpdateType::StillImageAvailable,
        .format = static_cast<int32_t>(ImageFormat::Webp),
        .capabilities = 0,
        .info = nullptr,
        .snapshot = nullptr,
        .animationFrame = nullptr,
    };
    update.snapshot = std::move(snapshot);
    sink(std::move(update));
  }

  IncrementalUpdate makeInfoUpdate(IncrementalUpdateType type) const {
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
            .width = static_cast<uint32_t>(config.input.width),
            .height = static_cast<uint32_t>(config.input.height),
            .outputWidth = outputDimensions.width,
            .outputHeight = outputDimensions.height,
            .isAnimated = false,
            .hasAlpha = config.input.has_alpha != 0});
    return update;
  }

  IncrementalDecodeOptionsNative options;
  IncrementalColorTransform colorTransform;
  WebPDecoderConfig config{};
  WebPIDecoder* decoder = nullptr;
  std::vector<uint8_t> pendingInput;
  std::vector<uint8_t> profileBytes;
  std::vector<uint8_t> decodedPixels;
  std::vector<uint8_t> transformedRow;
  std::unique_ptr<IncrementalImageCanvas> canvas;
  IncrementalOutputDimensions outputDimensions{};
  int publishedRows = 0;
  VP8StatusCode lastStatus = VP8_STATUS_SUSPENDED;
  bool complete = false;
};

class IncrementalWebpDecoder final : public IncrementalFormatDecoder {
public:
  explicit IncrementalWebpDecoder(const IncrementalDecodeOptionsNative& options)
      : options(options) {}

  IncrementalBackendResult append(const uint8_t* bytes, size_t size,
                                  bool endOfInput,
                                  const IncrementalUpdateSink& sink) override {
    if (delegate != nullptr) {
      return delegate->append(bytes, size, endOfInput, sink);
    }
    if (size > kMaximumWebpHeaderBytes - pendingInput.size()) {
      return IncrementalBackendResult::Unsupported;
    }
    if (size > 0) {
      pendingInput.insert(pendingInput.end(), bytes, bytes + size);
    }

    const auto animation = detectAnimation();
    if (!animation.has_value()) {
      if (endOfInput) {
        throw std::runtime_error("Truncated WebP header");
      }
      return IncrementalBackendResult::Accepted;
    }
    delegate = *animation
                   ? create_incremental_animated_webp_decoder(options)
                   : std::make_unique<IncrementalStaticWebpDecoder>(options);
    const auto result =
        delegate->append(pendingInput.empty() ? nullptr : pendingInput.data(),
                         pendingInput.size(), endOfInput, sink);
    pendingInput.clear();
    return result;
  }

private:
  std::optional<bool> detectAnimation() const {
    if (pendingInput.size() < 16) {
      return std::nullopt;
    }
    size_t offset = 12;
    while (offset + 8 <= pendingInput.size()) {
      const uint8_t* chunk = pendingInput.data() + offset;
      const uint32_t chunkSize = read_little_endian_u32(chunk + 4);
      if (std::memcmp(chunk, "VP8X", 4) == 0) {
        if (chunkSize < 10) {
          throw std::runtime_error("Invalid WebP extended header");
        }
        if (offset + 9 > pendingInput.size()) {
          return std::nullopt;
        }
        return (chunk[8] & 0x02) != 0;
      }
      if (std::memcmp(chunk, "ANIM", 4) == 0 ||
          std::memcmp(chunk, "ANMF", 4) == 0) {
        return true;
      }
      if (std::memcmp(chunk, "VP8 ", 4) == 0 ||
          std::memcmp(chunk, "VP8L", 4) == 0) {
        return false;
      }
      const uint64_t chunkEnd =
          static_cast<uint64_t>(offset) + 8 + chunkSize + (chunkSize & 1U);
      if (chunkEnd > pendingInput.size()) {
        return std::nullopt;
      }
      offset = static_cast<size_t>(chunkEnd);
    }
    return std::nullopt;
  }

  IncrementalDecodeOptionsNative options;
  std::vector<uint8_t> pendingInput;
  std::unique_ptr<IncrementalFormatDecoder> delegate;
};

} // namespace

std::unique_ptr<IncrementalFormatDecoder>
create_incremental_webp_decoder(const IncrementalDecodeOptionsNative& options) {
  return std::make_unique<IncrementalWebpDecoder>(options);
}
