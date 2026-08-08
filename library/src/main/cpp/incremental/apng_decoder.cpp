#include "incremental/apng_decoder.h"

#include "incremental/animation_canvas.h"
#include "incremental/color_transform.h"
#include "png.h"
#include "zlib.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

constexpr std::array<uint8_t, 8> kPngSignature = {0x89, 'P',  'N',  'G',
                                                  0x0D, 0x0A, 0x1A, 0x0A};
constexpr size_t kMaximumApngInputBytes = 256 * 1024 * 1024;

uint16_t read_big_endian_u16(const uint8_t* bytes) {
  return static_cast<uint16_t>(bytes[0]) << 8 | static_cast<uint16_t>(bytes[1]);
}

uint32_t read_big_endian_u32(const uint8_t* bytes) {
  return static_cast<uint32_t>(bytes[0]) << 24 |
         static_cast<uint32_t>(bytes[1]) << 16 |
         static_cast<uint32_t>(bytes[2]) << 8 | static_cast<uint32_t>(bytes[3]);
}

void append_big_endian_u32(std::vector<uint8_t>& output, uint32_t value) {
  output.push_back(static_cast<uint8_t>(value >> 24));
  output.push_back(static_cast<uint8_t>(value >> 16));
  output.push_back(static_cast<uint8_t>(value >> 8));
  output.push_back(static_cast<uint8_t>(value));
}

void append_png_chunk(std::vector<uint8_t>& output, const char* type,
                      const uint8_t* data, size_t size) {
  if (size > std::numeric_limits<uint32_t>::max()) {
    throw std::runtime_error("APNG frame chunk exceeds PNG limits");
  }
  append_big_endian_u32(output, static_cast<uint32_t>(size));
  const size_t typeOffset = output.size();
  output.insert(output.end(), type, type + 4);
  if (size > 0) {
    output.insert(output.end(), data, data + size);
  }
  uLong checksum = crc32(0, Z_NULL, 0);
  checksum = crc32(checksum, output.data() + typeOffset, 4 + size);
  append_big_endian_u32(output, static_cast<uint32_t>(checksum));
}

struct ApngFrameControl {
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t left = 0;
  uint32_t top = 0;
  uint64_t durationMillis = 0;
  IncrementalDisposalOperationNative disposal =
      IncrementalDisposalOperationNative::None;
  IncrementalBlendOperationNative blend =
      IncrementalBlendOperationNative::Source;
};

struct PngMemoryReader {
  const uint8_t* bytes;
  size_t size;
  size_t position = 0;
};

class IncrementalApngDecoder final : public IncrementalFormatDecoder {
public:
  explicit IncrementalApngDecoder(const IncrementalDecodeOptionsNative& options)
      : options(options) {}

  IncrementalBackendResult append(const uint8_t* bytes, size_t size,
                                  bool endOfInput,
                                  const IncrementalUpdateSink& sink) override {
    if (size > kMaximumApngInputBytes - input.size()) {
      return IncrementalBackendResult::Unsupported;
    }
    if (size > 0) {
      input.insert(input.end(), bytes, bytes + size);
    }
    parseAvailable(sink);
    if (resourceUnsupported) {
      return IncrementalBackendResult::Unsupported;
    }
    if (complete) {
      sink(makeInfoUpdate(IncrementalUpdateType::Complete));
      return IncrementalBackendResult::Complete;
    }
    if (endOfInput) {
      throw std::runtime_error("Truncated APNG input");
    }
    return IncrementalBackendResult::Accepted;
  }

private:
  void parseAvailable(const IncrementalUpdateSink& sink) {
    if (!signatureParsed) {
      if (input.size() < kPngSignature.size()) {
        return;
      }
      if (!std::equal(kPngSignature.begin(), kPngSignature.end(),
                      input.begin())) {
        throw std::runtime_error("Invalid APNG signature");
      }
      cursor = kPngSignature.size();
      signatureParsed = true;
    }

    while (input.size() - cursor >= 12) {
      const uint32_t size = read_big_endian_u32(input.data() + cursor);
      if (size > kMaximumApngInputBytes - 12 ||
          input.size() - cursor < static_cast<size_t>(size) + 12) {
        return;
      }
      const uint8_t* type = input.data() + cursor + 4;
      const uint8_t* data = input.data() + cursor + 8;
      validateChunk(type, data, size,
                    read_big_endian_u32(data + static_cast<size_t>(size)));
      processChunk(type, data, size, sink);
      cursor += static_cast<size_t>(size) + 12;
      if (complete || resourceUnsupported) {
        return;
      }
    }
  }

  void validateChunk(const uint8_t* type, const uint8_t* data, size_t size,
                     uint32_t expectedChecksum) const {
    uLong checksum = crc32(0, Z_NULL, 0);
    checksum = crc32(checksum, type, 4);
    checksum = crc32(checksum, data, size);
    if (static_cast<uint32_t>(checksum) != expectedChecksum) {
      throw std::runtime_error("APNG chunk checksum is invalid");
    }
  }

  void processChunk(const uint8_t* type, const uint8_t* data, size_t size,
                    const IncrementalUpdateSink& sink) {
    if (std::memcmp(type, "IHDR", 4) == 0) {
      parseHeader(data, size);
      return;
    }
    if (!headerParsed) {
      throw std::runtime_error("APNG chunk precedes IHDR");
    }
    if (std::memcmp(type, "acTL", 4) == 0) {
      parseAnimationControl(data, size, sink);
      return;
    }
    if (std::memcmp(type, "fcTL", 4) == 0) {
      beginFrame(data, size, sink);
      return;
    }
    if (std::memcmp(type, "IDAT", 4) == 0) {
      reachedImageData = true;
      if (frameControl.has_value()) {
        frameData.insert(frameData.end(), data, data + size);
      }
      return;
    }
    if (std::memcmp(type, "fdAT", 4) == 0) {
      if (size < 4 || !frameControl.has_value()) {
        throw std::runtime_error("APNG frame data has no frame control");
      }
      validateSequence(read_big_endian_u32(data));
      frameData.insert(frameData.end(), data + 4, data + size);
      return;
    }
    if (std::memcmp(type, "IEND", 4) == 0) {
      finishCurrentFrame(sink);
      if (resourceUnsupported) {
        return;
      }
      if (declaredFrameCount == 0 ||
          publishedFrameCount != declaredFrameCount) {
        throw std::runtime_error("APNG frame count does not match acTL");
      }
      complete = true;
      return;
    }

    if (!reachedImageData && std::memcmp(type, "acTL", 4) != 0 &&
        std::memcmp(type, "fcTL", 4) != 0) {
      append_png_chunk(sharedChunks, reinterpret_cast<const char*>(type), data,
                       size);
      if (std::memcmp(type, "tRNS", 4) == 0) {
        hasAlpha = true;
      }
    }
  }

  void parseHeader(const uint8_t* data, size_t size) {
    if (headerParsed || size != 13) {
      throw std::runtime_error("Invalid APNG IHDR chunk");
    }
    sourceWidth = read_big_endian_u32(data);
    sourceHeight = read_big_endian_u32(data + 4);
    std::copy(data, data + 13, headerData.begin());
    const uint8_t colorType = data[9];
    hasAlpha = colorType == PNG_COLOR_TYPE_GRAY_ALPHA ||
               colorType == PNG_COLOR_TYPE_RGB_ALPHA;
    headerParsed = true;
  }

  void parseAnimationControl(const uint8_t* data, size_t size,
                             const IncrementalUpdateSink& sink) {
    if (size != 8 || canvas != nullptr || reachedImageData) {
      throw std::runtime_error("Invalid APNG animation control");
    }
    declaredFrameCount = read_big_endian_u32(data);
    loopCount = read_big_endian_u32(data + 4);
    if (declaredFrameCount == 0 ||
        declaredFrameCount >
            static_cast<uint32_t>(std::numeric_limits<int32_t>::max()) ||
        loopCount >
            static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
      throw std::runtime_error("APNG animation counts exceed supported limits");
    }
    canvas = std::make_unique<IncrementalAnimationCanvas>(
        sourceWidth, sourceHeight, options);
    sink(makeInfoUpdate(IncrementalUpdateType::MetadataAvailable));
  }

  void beginFrame(const uint8_t* data, size_t size,
                  const IncrementalUpdateSink& sink) {
    if (canvas == nullptr || size != 26) {
      throw std::runtime_error("Invalid APNG frame control");
    }
    finishCurrentFrame(sink);
    if (resourceUnsupported) {
      return;
    }
    validateSequence(read_big_endian_u32(data));

    ApngFrameControl control;
    control.width = read_big_endian_u32(data + 4);
    control.height = read_big_endian_u32(data + 8);
    control.left = read_big_endian_u32(data + 12);
    control.top = read_big_endian_u32(data + 16);
    const uint16_t delayNumerator = read_big_endian_u16(data + 20);
    uint16_t delayDenominator = read_big_endian_u16(data + 22);
    if (delayDenominator == 0) {
      delayDenominator = 100;
    }
    control.durationMillis =
        (static_cast<uint64_t>(delayNumerator) * 1000 + delayDenominator / 2) /
        delayDenominator;
    switch (data[24]) {
    case 0:
      control.disposal = IncrementalDisposalOperationNative::None;
      break;
    case 1:
      control.disposal = IncrementalDisposalOperationNative::Background;
      break;
    case 2:
      control.disposal = publishedFrameCount == 0
                             ? IncrementalDisposalOperationNative::Background
                             : IncrementalDisposalOperationNative::Previous;
      break;
    default:
      throw std::runtime_error("Invalid APNG disposal operation");
    }
    switch (data[25]) {
    case 0:
      control.blend = IncrementalBlendOperationNative::Source;
      break;
    case 1:
      control.blend = IncrementalBlendOperationNative::Over;
      break;
    default:
      throw std::runtime_error("Invalid APNG blend operation");
    }
    canvas->mapRegion(control.left, control.top, control.width, control.height);
    frameControl = control;
  }

  void validateSequence(uint32_t sequence) {
    if (sequence != nextSequence) {
      throw std::runtime_error("APNG sequence number is invalid");
    }
    ++nextSequence;
  }

  void finishCurrentFrame(const IncrementalUpdateSink& sink) {
    if (!frameControl.has_value()) {
      return;
    }
    if (frameData.empty()) {
      throw std::runtime_error("APNG frame has no image data");
    }
    const uint64_t sourceFramePixels =
        static_cast<uint64_t>(frameControl->width) * frameControl->height;
    if (sourceFramePixels > options.maximumBitmapPixels ||
        sourceFramePixels > std::numeric_limits<size_t>::max() / 4) {
      resourceUnsupported = true;
      return;
    }

    const auto region =
        canvas->mapRegion(frameControl->left, frameControl->top,
                          frameControl->width, frameControl->height);
    auto decoded = decodeFrame(*frameControl, region);
    canvas->beginFrame(region, frameControl->disposal);
    canvas->composite(region, decoded.data(), frameControl->blend);
    auto update = canvas->makeFrameUpdate(
        ImageFormat::Png, static_cast<int32_t>(publishedFrameCount),
        frameControl->durationMillis, region, frameControl->blend,
        frameControl->disposal);
    sink(std::move(*update));
    canvas->disposeFrame(region, frameControl->disposal);
    ++publishedFrameCount;
    frameControl.reset();
    frameData.clear();
  }

  std::vector<uint8_t>
  decodeFrame(const ApngFrameControl& control,
              const IncrementalAnimationRegion& region) const {
    std::vector<uint8_t> pngBytes(kPngSignature.begin(), kPngSignature.end());
    auto frameHeader = headerData;
    frameHeader[0] = static_cast<uint8_t>(control.width >> 24);
    frameHeader[1] = static_cast<uint8_t>(control.width >> 16);
    frameHeader[2] = static_cast<uint8_t>(control.width >> 8);
    frameHeader[3] = static_cast<uint8_t>(control.width);
    frameHeader[4] = static_cast<uint8_t>(control.height >> 24);
    frameHeader[5] = static_cast<uint8_t>(control.height >> 16);
    frameHeader[6] = static_cast<uint8_t>(control.height >> 8);
    frameHeader[7] = static_cast<uint8_t>(control.height);
    append_png_chunk(pngBytes, "IHDR", frameHeader.data(), frameHeader.size());
    pngBytes.insert(pngBytes.end(), sharedChunks.begin(), sharedChunks.end());
    append_png_chunk(pngBytes, "IDAT", frameData.data(), frameData.size());
    append_png_chunk(pngBytes, "IEND", nullptr, 0);

    png_struct* png = png_create_read_struct(
        PNG_LIBPNG_VER_STRING, nullptr,
        [](png_struct*, png_const_charp message) {
          throw std::runtime_error(message);
        },
        nullptr);
    if (png == nullptr) {
      throw std::runtime_error("Failed to create APNG frame reader");
    }
    png_info* info = png_create_info_struct(png);
    if (info == nullptr) {
      png_destroy_read_struct(&png, nullptr, nullptr);
      throw std::runtime_error("Failed to create APNG frame info");
    }

    try {
      PngMemoryReader reader{pngBytes.data(), pngBytes.size()};
      png_set_read_fn(
          png, &reader, [](png_struct* png, png_byte* output, png_size_t size) {
            auto* reader = static_cast<PngMemoryReader*>(png_get_io_ptr(png));
            if (size > reader->size - reader->position) {
              png_error(png, "Truncated synthesized APNG frame");
            }
            std::memcpy(output, reader->bytes + reader->position, size);
            reader->position += size;
          });
      png_read_info(png, info);
      const uint8_t colorType = png_get_color_type(png, info);
      const uint8_t bitDepth = png_get_bit_depth(png, info);
      const bool grayscale = colorType == PNG_COLOR_TYPE_GRAY ||
                             colorType == PNG_COLOR_TYPE_GRAY_ALPHA;
      cmsHPROFILE sourceProfile = nullptr;
      if (png_get_valid(png, info, PNG_INFO_iCCP) != 0) {
        png_charp name;
        png_bytep profileBytes;
        png_uint_32 profileSize;
        int compressionType;
        png_get_iCCP(png, info, &name, &compressionType, &profileBytes,
                     &profileSize);
        sourceProfile = cmsOpenProfileFromMem(profileBytes, profileSize);
        if (sourceProfile != nullptr) {
          const auto colorSpace = cmsGetColorSpace(sourceProfile);
          if ((grayscale && colorSpace != cmsSigGrayData) ||
              (!grayscale && colorSpace != cmsSigRgbData)) {
            cmsCloseProfile(sourceProfile);
            sourceProfile = nullptr;
          }
        }
      }

      png_set_expand(png);
      if (bitDepth == 16) {
        png_set_scale_16(png);
      }
      if (grayscale && sourceProfile == nullptr) {
        png_set_gray_to_rgb(png);
      }
      const bool alpha = (colorType & PNG_COLOR_MASK_ALPHA) != 0 ||
                         png_get_valid(png, info, PNG_INFO_tRNS) != 0;
      if (!alpha) {
        png_set_add_alpha(png, 0xFF, PNG_FILLER_AFTER);
      }
      if (sourceProfile == nullptr) {
        sourceProfile = cmsCreate_sRGBProfile();
      }
      const cmsUInt32Number inputType =
          grayscale && cmsGetColorSpace(sourceProfile) == cmsSigGrayData
              ? TYPE_GRAYA_8
              : TYPE_RGBA_8;
      IncrementalColorTransform colorTransform(options);
      colorTransform.configure(sourceProfile, inputType, true, false);

      png_set_interlace_handling(png);
      png_read_update_info(png, info);
      const size_t rowBytes = png_get_rowbytes(png, info);
      std::vector<uint8_t> sourcePixels(rowBytes * control.height);
      std::vector<png_bytep> rows(control.height);
      for (uint32_t row = 0; row < control.height; ++row) {
        rows[row] = sourcePixels.data() + static_cast<size_t>(row) * rowBytes;
      }
      png_read_image(png, rows.data());
      png_read_end(png, info);

      std::vector<uint8_t> output(static_cast<size_t>(region.outputWidth) *
                                  region.outputHeight * 4);
      std::vector<uint8_t> transformedRow(static_cast<size_t>(control.width) *
                                          4);
      for (uint32_t outputY = 0; outputY < region.outputHeight; ++outputY) {
        const uint32_t sourceY = std::min<uint32_t>(
            control.height - 1,
            (static_cast<uint64_t>(outputY) * 2 + 1) * control.height /
                (static_cast<uint64_t>(region.outputHeight) * 2));
        const uint8_t* sourceRow =
            sourcePixels.data() + static_cast<size_t>(sourceY) * rowBytes;
        colorTransform.apply(sourceRow, transformedRow.data(), control.width);
        uint8_t* outputRow = output.data() + static_cast<size_t>(outputY) *
                                                 region.outputWidth * 4;
        for (uint32_t outputX = 0; outputX < region.outputWidth; ++outputX) {
          const uint32_t sourceX = std::min<uint32_t>(
              control.width - 1,
              (static_cast<uint64_t>(outputX) * 2 + 1) * control.width /
                  (static_cast<uint64_t>(region.outputWidth) * 2));
          std::memcpy(outputRow + outputX * 4,
                      transformedRow.data() + sourceX * 4, 4);
        }
      }
      png_destroy_read_struct(&png, &info, nullptr);
      return output;
    } catch (...) {
      png_destroy_read_struct(&png, &info, nullptr);
      throw;
    }
  }

  IncrementalUpdate makeInfoUpdate(IncrementalUpdateType type) const {
    const auto& output = canvas->outputDimensions();
    IncrementalUpdate update{
        .type = type,
        .format = static_cast<int32_t>(ImageFormat::Png),
        .capabilities = 0,
        .info = nullptr,
        .snapshot = nullptr,
        .animationFrame = nullptr,
    };
    update.info =
        std::make_unique<IncrementalImageInfoNative>(IncrementalImageInfoNative{
            .format = ImageFormat::Png,
            .width = sourceWidth,
            .height = sourceHeight,
            .outputWidth = output.width,
            .outputHeight = output.height,
            .isAnimated = true,
            .hasAlpha = hasAlpha,
            .frameCount = static_cast<int32_t>(declaredFrameCount),
            .loopCount = static_cast<int32_t>(loopCount),
        });
    return update;
  }

  IncrementalDecodeOptionsNative options;
  std::vector<uint8_t> input;
  std::vector<uint8_t> sharedChunks;
  std::vector<uint8_t> frameData;
  std::unique_ptr<IncrementalAnimationCanvas> canvas;
  std::optional<ApngFrameControl> frameControl;
  std::array<uint8_t, 13> headerData{};
  size_t cursor = 0;
  uint32_t sourceWidth = 0;
  uint32_t sourceHeight = 0;
  uint32_t declaredFrameCount = 0;
  uint32_t loopCount = 0;
  uint32_t nextSequence = 0;
  uint32_t publishedFrameCount = 0;
  bool signatureParsed = false;
  bool headerParsed = false;
  bool reachedImageData = false;
  bool complete = false;
  bool hasAlpha = false;
  bool resourceUnsupported = false;
};

} // namespace

std::unique_ptr<IncrementalFormatDecoder>
create_incremental_apng_decoder(const IncrementalDecodeOptionsNative& options) {
  return std::make_unique<IncrementalApngDecoder>(options);
}
