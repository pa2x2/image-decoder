#include "incremental/png_decoder.h"

#include "incremental/apng_decoder.h"
#include "incremental/color_transform.h"
#include "incremental/image_canvas.h"
#include "log.h"
#include "png.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

uint32_t read_big_endian_u32(const uint8_t* bytes) {
  return static_cast<uint32_t>(bytes[0]) << 24 |
         static_cast<uint32_t>(bytes[1]) << 16 |
         static_cast<uint32_t>(bytes[2]) << 8 | static_cast<uint32_t>(bytes[3]);
}

class PngChunkScanner {
public:
  void append(const uint8_t* bytes, size_t size) {
    while (size > 0 && !reachedImageData) {
      if (signatureBytes < 8) {
        const size_t count = std::min<size_t>(8 - signatureBytes, size);
        signatureBytes += count;
        bytes += count;
        size -= count;
        continue;
      }
      if (chunkBytesRemaining > 0) {
        const size_t count = std::min<uint64_t>(chunkBytesRemaining, size);
        chunkBytesRemaining -= count;
        bytes += count;
        size -= count;
        continue;
      }

      const size_t count = std::min<size_t>(header.size() - headerBytes, size);
      std::memcpy(header.data() + headerBytes, bytes, count);
      headerBytes += count;
      bytes += count;
      size -= count;
      if (headerBytes != header.size()) {
        continue;
      }

      const uint32_t chunkLength = read_big_endian_u32(header.data());
      const char* chunkType = reinterpret_cast<const char*>(header.data() + 4);
      if (std::memcmp(chunkType, "acTL", 4) == 0) {
        animated = true;
      }
      if (std::memcmp(chunkType, "IDAT", 4) == 0) {
        reachedImageData = true;
      }
      chunkBytesRemaining = static_cast<uint64_t>(chunkLength) + 4;
      headerBytes = 0;
    }
  }

  bool isAnimated() const { return animated; }

private:
  std::array<uint8_t, 8> header{};
  size_t signatureBytes = 0;
  size_t headerBytes = 0;
  uint64_t chunkBytesRemaining = 0;
  bool reachedImageData = false;
  bool animated = false;
};

class IncrementalStaticPngDecoder final : public IncrementalFormatDecoder {
public:
  explicit IncrementalStaticPngDecoder(
      const IncrementalDecodeOptionsNative& options)
      : options(options), colorTransform(options) {
    png = png_create_read_struct(
        PNG_LIBPNG_VER_STRING, nullptr,
        [](png_struct*, png_const_charp message) {
          throw std::runtime_error(message);
        },
        [](png_struct*, png_const_charp message) { LOGW("%s", message); });
    if (png == nullptr) {
      throw std::runtime_error("Failed to create incremental PNG reader");
    }
    info = png_create_info_struct(png);
    if (info == nullptr) {
      png_destroy_read_struct(&png, nullptr, nullptr);
      throw std::runtime_error("Failed to create incremental PNG info");
    }
    png_set_progressive_read_fn(
        png, this,
        [](png_struct* png, png_info* info) {
          auto* decoder = static_cast<IncrementalStaticPngDecoder*>(
              png_get_progressive_ptr(png));
          decoder->onInfo(info);
        },
        [](png_struct* png, png_byte* row, png_uint_32 rowNumber, int pass) {
          auto* decoder = static_cast<IncrementalStaticPngDecoder*>(
              png_get_progressive_ptr(png));
          decoder->onRow(row, rowNumber, pass);
        },
        [](png_struct* png, png_info*) {
          auto* decoder = static_cast<IncrementalStaticPngDecoder*>(
              png_get_progressive_ptr(png));
          decoder->complete = true;
        });
  }

  ~IncrementalStaticPngDecoder() override {
    png_destroy_read_struct(&png, &info, nullptr);
  }

  IncrementalBackendResult append(const uint8_t* bytes, size_t size,
                                  bool endOfInput,
                                  const IncrementalUpdateSink& sink) override {
    chunkScanner.append(bytes, size);
    if (chunkScanner.isAnimated()) {
      return IncrementalBackendResult::Unsupported;
    }
    if (size > 0) {
      png_process_data(png, info, const_cast<png_bytep>(bytes), size);
    }
    publishSnapshot();
    for (auto& update : pendingUpdates) {
      sink(std::move(update));
    }
    pendingUpdates.clear();

    if (complete) {
      sink(makeInfoUpdate(IncrementalUpdateType::Complete));
      return IncrementalBackendResult::Complete;
    }
    if (endOfInput) {
      throw std::runtime_error("Truncated PNG input");
    }
    return IncrementalBackendResult::Accepted;
  }

private:
  void onInfo(png_info* parsedInfo) {
    sourceWidth = png_get_image_width(png, parsedInfo);
    sourceHeight = png_get_image_height(png, parsedInfo);
    const uint8_t colorType = png_get_color_type(png, parsedInfo);
    const uint8_t bitDepth = png_get_bit_depth(png, parsedInfo);
    interlaced = png_get_interlace_type(png, parsedInfo) == PNG_INTERLACE_ADAM7;
    hasAlpha = (colorType & PNG_COLOR_MASK_ALPHA) != 0 ||
               png_get_valid(png, parsedInfo, PNG_INFO_tRNS) != 0;

    cmsHPROFILE sourceProfile = readColorProfile(parsedInfo, colorType);
    const bool grayscale = colorType == PNG_COLOR_TYPE_GRAY ||
                           colorType == PNG_COLOR_TYPE_GRAY_ALPHA;

    png_set_expand(png);
    if (bitDepth == 16) {
      png_set_scale_16(png);
    }
    if (grayscale && sourceProfile == nullptr) {
      png_set_gray_to_rgb(png);
    }
    if (!hasAlpha) {
      png_set_add_alpha(png, 0xFF, PNG_FILLER_AFTER);
    }

    if (sourceProfile == nullptr) {
      sourceProfile = cmsCreate_sRGBProfile();
    }
    if (grayscale && cmsGetColorSpace(sourceProfile) == cmsSigGrayData) {
      inputType = TYPE_GRAYA_8;
      inputComponents = 2;
    } else {
      inputType = TYPE_RGBA_8;
      inputComponents = 4;
    }
    colorTransform.configure(sourceProfile, inputType, true, false);

    png_set_interlace_handling(png);
    png_read_update_info(png, parsedInfo);
    const size_t rowBytes = png_get_rowbytes(png, parsedInfo);
    const size_t expectedRowBytes =
        static_cast<size_t>(sourceWidth) * inputComponents;
    if (rowBytes < expectedRowBytes) {
      throw std::runtime_error("Unexpected incremental PNG row size");
    }

    rgbaRow.resize(static_cast<size_t>(sourceWidth) * 4);
    outputDimensions = calculate_incremental_output_dimensions(
        sourceWidth, sourceHeight, options);
    canvas = std::make_unique<IncrementalImageCanvas>(sourceWidth, sourceHeight,
                                                      outputDimensions);
    pendingUpdates.push_back(
        makeInfoUpdate(IncrementalUpdateType::MetadataAvailable));
  }

  cmsHPROFILE readColorProfile(png_info* parsedInfo, uint8_t colorType) {
    if (png_get_valid(png, parsedInfo, PNG_INFO_iCCP) == 0) {
      return nullptr;
    }
    png_charp name;
    png_bytep profileBytes;
    png_uint_32 profileSize;
    int compressionType;
    png_get_iCCP(png, parsedInfo, &name, &compressionType, &profileBytes,
                 &profileSize);
    cmsHPROFILE profile = cmsOpenProfileFromMem(profileBytes, profileSize);
    if (profile == nullptr) {
      return nullptr;
    }
    const bool grayscale = colorType == PNG_COLOR_TYPE_GRAY ||
                           colorType == PNG_COLOR_TYPE_GRAY_ALPHA;
    const cmsColorSpaceSignature space = cmsGetColorSpace(profile);
    if ((grayscale && space != cmsSigGrayData) ||
        (!grayscale && space != cmsSigRgbData)) {
      cmsCloseProfile(profile);
      return nullptr;
    }
    return profile;
  }

  void onRow(png_byte* row, png_uint_32 rowNumber, int pass) {
    if (row == nullptr || canvas == nullptr) {
      return;
    }
    if (!interlaced) {
      colorTransform.apply(row, rgbaRow.data(), sourceWidth);
      canvas->updateSourceRow(rowNumber, rgbaRow.data());
      return;
    }
    uint32_t outputY;
    if (!canvas->outputRowForSource(rowNumber, &outputY)) {
      return;
    }
    colorTransform.apply(row, rgbaRow.data(), sourceWidth);

    const bool exactRow = PNG_ROW_IN_INTERLACE_PASS(rowNumber, pass);
    for (uint32_t outputX = 0; outputX < canvas->outputWidth(); ++outputX) {
      const uint32_t sourceX = canvas->sourceXForOutput(outputX);
      const bool exactPixel =
          exactRow && PNG_COL_IN_INTERLACE_PASS(sourceX, pass);
      canvas->updatePixel(outputX, outputY, rgbaRow.data() + sourceX * 4,
                          exactPixel);
    }
  }

  IncrementalUpdate makeInfoUpdate(IncrementalUpdateType type) const {
    IncrementalUpdate update{
        .type = type,
        .format = static_cast<int32_t>(ImageFormat::Png),
        .capabilities = 0,
        .info = nullptr,
        .snapshot = nullptr,
        .animationFrame = nullptr,
    };
    update.info = std::make_unique<IncrementalImageInfoNative>(
        IncrementalImageInfoNative{.format = ImageFormat::Png,
                                   .width = sourceWidth,
                                   .height = sourceHeight,
                                   .outputWidth = outputDimensions.width,
                                   .outputHeight = outputDimensions.height,
                                   .isAnimated = false,
                                   .hasAlpha = hasAlpha});
    return update;
  }

  void publishSnapshot() {
    if (canvas == nullptr) {
      return;
    }
    auto snapshot = canvas->takeSnapshot();
    if (snapshot == nullptr) {
      return;
    }
    IncrementalUpdate update{
        .type = IncrementalUpdateType::StillImageAvailable,
        .format = static_cast<int32_t>(ImageFormat::Png),
        .capabilities = 0,
        .info = nullptr,
        .snapshot = nullptr,
        .animationFrame = nullptr,
    };
    update.snapshot = std::move(snapshot);
    pendingUpdates.push_back(std::move(update));
  }

  IncrementalDecodeOptionsNative options;
  IncrementalColorTransform colorTransform;
  PngChunkScanner chunkScanner;
  png_struct* png = nullptr;
  png_info* info = nullptr;
  std::unique_ptr<IncrementalImageCanvas> canvas;
  std::vector<uint8_t> rgbaRow;
  std::vector<IncrementalUpdate> pendingUpdates;
  IncrementalOutputDimensions outputDimensions{};
  cmsUInt32Number inputType = TYPE_RGBA_8;
  uint32_t sourceWidth = 0;
  uint32_t sourceHeight = 0;
  uint32_t inputComponents = 4;
  bool interlaced = false;
  bool hasAlpha = false;
  bool complete = false;
};

class IncrementalPngDecoder final : public IncrementalFormatDecoder {
public:
  explicit IncrementalPngDecoder(const IncrementalDecodeOptionsNative& options)
      : options(options) {}

  IncrementalBackendResult append(const uint8_t* bytes, size_t size,
                                  bool endOfInput,
                                  const IncrementalUpdateSink& sink) override {
    if (delegate != nullptr) {
      return delegate->append(bytes, size, endOfInput, sink);
    }
    if (size > kMaximumPngHeaderBytes - pendingInput.size()) {
      return IncrementalBackendResult::Unsupported;
    }
    if (size > 0) {
      pendingInput.insert(pendingInput.end(), bytes, bytes + size);
    }
    const auto animation = detectAnimation();
    if (!animation.has_value()) {
      if (endOfInput) {
        throw std::runtime_error("Truncated PNG header");
      }
      return IncrementalBackendResult::Accepted;
    }
    delegate = *animation
                   ? create_incremental_apng_decoder(options)
                   : std::make_unique<IncrementalStaticPngDecoder>(options);
    const auto result =
        delegate->append(pendingInput.empty() ? nullptr : pendingInput.data(),
                         pendingInput.size(), endOfInput, sink);
    pendingInput.clear();
    return result;
  }

private:
  std::optional<bool> detectAnimation() const {
    if (pendingInput.size() < 8) {
      return std::nullopt;
    }
    size_t offset = 8;
    while (offset + 8 <= pendingInput.size()) {
      const uint32_t size = read_big_endian_u32(pendingInput.data() + offset);
      const uint8_t* type = pendingInput.data() + offset + 4;
      if (std::memcmp(type, "acTL", 4) == 0) {
        return true;
      }
      if (std::memcmp(type, "IDAT", 4) == 0) {
        return false;
      }
      const uint64_t end = static_cast<uint64_t>(offset) + size + 12;
      if (end > pendingInput.size()) {
        return std::nullopt;
      }
      offset = static_cast<size_t>(end);
    }
    return std::nullopt;
  }

  static constexpr size_t kMaximumPngHeaderBytes = 4 * 1024 * 1024;
  IncrementalDecodeOptionsNative options;
  std::vector<uint8_t> pendingInput;
  std::unique_ptr<IncrementalFormatDecoder> delegate;
};

} // namespace

std::unique_ptr<IncrementalFormatDecoder>
create_incremental_png_decoder(const IncrementalDecodeOptionsNative& options) {
  return std::make_unique<IncrementalPngDecoder>(options);
}
