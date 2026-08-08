#include "incremental/gif_decoder.h"

#include "incremental/animation_canvas.h"
#include "incremental/color_transform.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr size_t kMaximumGifInputBytes = 256 * 1024 * 1024;
constexpr uint32_t kMaximumGifCodeCount = 4096;

uint16_t read_little_endian_u16(const uint8_t* bytes) {
  return static_cast<uint16_t>(bytes[0]) | static_cast<uint16_t>(bytes[1]) << 8;
}

struct GifGraphicControl {
  uint64_t durationMillis = 0;
  IncrementalDisposalOperationNative disposal =
      IncrementalDisposalOperationNative::None;
  int32_t transparentIndex = -1;
};

class GifLzwReader {
public:
  GifLzwReader(const uint8_t* bytes, size_t size, uint8_t minimumCodeSize)
      : bytes(bytes), size(size), clearCode(1U << minimumCodeSize),
        endCode(clearCode + 1), nextCode(endCode + 1),
        initialCodeSize(minimumCodeSize + 1), codeSize(initialCodeSize) {
    if (minimumCodeSize < 2 || minimumCodeSize > 8) {
      throw std::runtime_error("Invalid GIF LZW code size");
    }
    resetDictionary();
  }

  std::vector<uint8_t> decode(size_t expectedPixels) {
    std::vector<uint8_t> output;
    output.reserve(std::min<size_t>(expectedPixels, 1024 * 1024));
    int32_t previousCode = -1;
    uint8_t previousFirst = 0;

    while (true) {
      const uint32_t code = readCode();
      if (code == clearCode) {
        resetDictionary();
        previousCode = -1;
        continue;
      }
      if (code == endCode) {
        break;
      }
      if (code > nextCode || code >= kMaximumGifCodeCount) {
        throw std::runtime_error("Invalid GIF LZW code");
      }

      uint32_t currentCode = code;
      size_t stackSize = 0;
      if (currentCode == nextCode) {
        if (previousCode < 0) {
          throw std::runtime_error("Invalid GIF LZW dictionary reference");
        }
        stack[stackSize++] = previousFirst;
        currentCode = static_cast<uint32_t>(previousCode);
      }
      while (currentCode >= clearCode) {
        if (currentCode >= nextCode || stackSize >= stack.size()) {
          throw std::runtime_error("Invalid GIF LZW dictionary chain");
        }
        stack[stackSize++] = suffix[currentCode];
        currentCode = prefix[currentCode];
      }
      const uint8_t first = static_cast<uint8_t>(currentCode);
      stack[stackSize++] = first;
      while (stackSize > 0) {
        if (output.size() >= expectedPixels) {
          throw std::runtime_error("GIF frame contains excess pixels");
        }
        output.push_back(stack[--stackSize]);
      }

      if (previousCode >= 0 && nextCode < kMaximumGifCodeCount) {
        prefix[nextCode] = static_cast<uint16_t>(previousCode);
        suffix[nextCode] = first;
        ++nextCode;
        if (nextCode == (1U << codeSize) && codeSize < 12) {
          ++codeSize;
        }
      }
      previousCode = static_cast<int32_t>(code);
      previousFirst = first;
    }
    if (output.size() != expectedPixels) {
      throw std::runtime_error("GIF frame pixel count is incomplete");
    }
    return output;
  }

private:
  uint32_t readCode() {
    while (bitCount < codeSize) {
      if (position >= size) {
        throw std::runtime_error("Truncated GIF LZW stream");
      }
      bitBuffer |= static_cast<uint32_t>(bytes[position++]) << bitCount;
      bitCount += 8;
    }
    const uint32_t code = bitBuffer & ((1U << codeSize) - 1);
    bitBuffer >>= codeSize;
    bitCount -= codeSize;
    return code;
  }

  void resetDictionary() {
    nextCode = endCode + 1;
    codeSize = initialCodeSize;
  }

  const uint8_t* bytes;
  size_t size;
  std::array<uint16_t, kMaximumGifCodeCount> prefix{};
  std::array<uint8_t, kMaximumGifCodeCount> suffix{};
  std::array<uint8_t, kMaximumGifCodeCount + 1> stack{};
  uint32_t clearCode;
  uint32_t endCode;
  uint32_t nextCode;
  uint32_t initialCodeSize;
  uint32_t codeSize;
  size_t position = 0;
  uint32_t bitBuffer = 0;
  uint32_t bitCount = 0;
};

class IncrementalGifDecoder final : public IncrementalFormatDecoder {
public:
  explicit IncrementalGifDecoder(const IncrementalDecodeOptionsNative& options)
      : options(options), colorTransform(options) {}

  IncrementalBackendResult append(const uint8_t* bytes, size_t size,
                                  bool endOfInput,
                                  const IncrementalUpdateSink& sink) override {
    if (size > kMaximumGifInputBytes - input.size()) {
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
      throw std::runtime_error("Truncated GIF input");
    }
    return IncrementalBackendResult::Accepted;
  }

private:
  void parseAvailable(const IncrementalUpdateSink& sink) {
    if (!headerParsed && !parseHeader(sink)) {
      return;
    }
    while (cursor < input.size()) {
      const size_t recordStart = cursor;
      switch (input[cursor++]) {
      case 0x2C:
        if (!parseImage(sink)) {
          cursor = recordStart;
          return;
        }
        if (resourceUnsupported) {
          return;
        }
        break;
      case 0x21:
        if (!parseExtension()) {
          cursor = recordStart;
          return;
        }
        break;
      case 0x3B:
        complete = true;
        return;
      default:
        throw std::runtime_error("Unknown GIF record type");
      }
    }
  }

  bool parseHeader(const IncrementalUpdateSink& sink) {
    if (input.size() < 13) {
      return false;
    }
    if (std::memcmp(input.data(), "GIF87a", 6) != 0 &&
        std::memcmp(input.data(), "GIF89a", 6) != 0) {
      throw std::runtime_error("Invalid GIF signature");
    }
    sourceWidth = read_little_endian_u16(input.data() + 6);
    sourceHeight = read_little_endian_u16(input.data() + 8);
    const uint8_t packed = input[10];
    const bool hasGlobalPalette = (packed & 0x80) != 0;
    const size_t globalPaletteSize =
        hasGlobalPalette ? (2U << (packed & 0x07)) : 0;
    const size_t headerSize = 13 + globalPaletteSize * 3;
    if (input.size() < headerSize) {
      return false;
    }
    globalPalette = readPalette(input.data() + 13, globalPaletteSize);

    std::array<uint8_t, 4> background{0, 0, 0, 0};
    const uint8_t backgroundIndex = input[11];
    if (backgroundIndex < globalPalette.size()) {
      background = globalPalette[backgroundIndex];
    }
    cmsHPROFILE sourceProfile = cmsCreate_sRGBProfile();
    colorTransform.configure(sourceProfile, TYPE_RGBA_8, true, false);
    std::array<uint8_t, 4> transformedBackground{};
    colorTransform.apply(background.data(), transformedBackground.data(), 1);
    canvas = std::make_unique<IncrementalAnimationCanvas>(
        sourceWidth, sourceHeight, options, transformedBackground);
    cursor = headerSize;
    headerParsed = true;
    sink(makeInfoUpdate(IncrementalUpdateType::MetadataAvailable));
    return true;
  }

  bool parseExtension() {
    if (cursor >= input.size()) {
      return false;
    }
    const uint8_t label = input[cursor++];
    if (label == 0xF9) {
      if (input.size() - cursor < 6) {
        return false;
      }
      if (input[cursor] != 4 || input[cursor + 5] != 0) {
        throw std::runtime_error("Invalid GIF graphic control extension");
      }
      const uint8_t packed = input[cursor + 1];
      const uint8_t disposal = (packed >> 2) & 0x07;
      pendingControl.durationMillis =
          static_cast<uint64_t>(
              read_little_endian_u16(input.data() + cursor + 2)) *
          10;
      pendingControl.disposal =
          disposal == 2   ? IncrementalDisposalOperationNative::Background
          : disposal == 3 ? IncrementalDisposalOperationNative::Previous
                          : IncrementalDisposalOperationNative::None;
      pendingControl.transparentIndex =
          (packed & 0x01) != 0 ? input[cursor + 4] : -1;
      hasAlpha = hasAlpha || pendingControl.transparentIndex >= 0;
      cursor += 6;
      return true;
    }

    if (cursor >= input.size()) {
      return false;
    }
    const size_t fixedSize = input[cursor];
    if (input.size() - cursor < fixedSize + 1) {
      return false;
    }
    const uint8_t* fixedData = input.data() + cursor + 1;
    cursor += fixedSize + 1;
    std::vector<uint8_t> blocks;
    if (!readSubBlocks(blocks)) {
      return false;
    }
    if (label == 0xFF && fixedSize == 11 && blocks.size() >= 3 &&
        (std::string_view(reinterpret_cast<const char*>(fixedData), 11) ==
             "NETSCAPE2.0" ||
         std::string_view(reinterpret_cast<const char*>(fixedData), 11) ==
             "ANIMEXTS1.0") &&
        blocks[0] == 1) {
      const uint16_t repetitions = read_little_endian_u16(blocks.data() + 1);
      loopCount = repetitions == 0 ? 0 : repetitions + 1;
    }
    return true;
  }

  bool parseImage(const IncrementalUpdateSink& sink) {
    if (input.size() - cursor < 9) {
      return false;
    }
    const uint32_t left = read_little_endian_u16(input.data() + cursor);
    const uint32_t top = read_little_endian_u16(input.data() + cursor + 2);
    const uint32_t width = read_little_endian_u16(input.data() + cursor + 4);
    const uint32_t height = read_little_endian_u16(input.data() + cursor + 6);
    const uint8_t packed = input[cursor + 8];
    cursor += 9;

    const bool hasLocalPalette = (packed & 0x80) != 0;
    const bool interlaced = (packed & 0x40) != 0;
    const size_t localPaletteSize =
        hasLocalPalette ? (2U << (packed & 0x07)) : 0;
    if (input.size() - cursor < localPaletteSize * 3 + 1) {
      return false;
    }
    auto localPalette = readPalette(input.data() + cursor, localPaletteSize);
    cursor += localPaletteSize * 3;
    const uint8_t minimumCodeSize = input[cursor++];
    std::vector<uint8_t> compressed;
    if (!readSubBlocks(compressed)) {
      return false;
    }

    const auto& palette = hasLocalPalette ? localPalette : globalPalette;
    if (palette.empty()) {
      throw std::runtime_error("GIF frame has no color table");
    }
    const uint64_t sourceFramePixels = static_cast<uint64_t>(width) * height;
    const uint64_t maximumSourceFramePixels =
        std::min<uint64_t>(32 * 1024 * 1024, options.maximumBitmapPixels * 2);
    if (sourceFramePixels > maximumSourceFramePixels ||
        sourceFramePixels > std::numeric_limits<size_t>::max()) {
      resourceUnsupported = true;
      return true;
    }
    GifLzwReader lzw(compressed.data(), compressed.size(), minimumCodeSize);
    const auto indices =
        lzw.decode(static_cast<size_t>(width) * static_cast<size_t>(height));
    publishFrame(left, top, width, height, interlaced, palette, indices, sink);
    pendingControl = {};
    return true;
  }

  void publishFrame(uint32_t left, uint32_t top, uint32_t width,
                    uint32_t height, bool interlaced,
                    const std::vector<std::array<uint8_t, 4>>& palette,
                    const std::vector<uint8_t>& indices,
                    const IncrementalUpdateSink& sink) {
    const auto region = canvas->mapRegion(left, top, width, height);
    std::vector<uint32_t> encodedRowForSource(height);
    uint32_t encodedRow = 0;
    if (interlaced) {
      for (uint32_t row = 0; row < height; row += 8) {
        encodedRowForSource[row] = encodedRow++;
      }
      for (uint32_t row = 4; row < height; row += 8) {
        encodedRowForSource[row] = encodedRow++;
      }
      for (uint32_t row = 2; row < height; row += 4) {
        encodedRowForSource[row] = encodedRow++;
      }
      for (uint32_t row = 1; row < height; row += 2) {
        encodedRowForSource[row] = encodedRow++;
      }
    } else {
      for (uint32_t row = 0; row < height; ++row) {
        encodedRowForSource[row] = row;
      }
    }

    std::vector<uint8_t> framePixels(static_cast<size_t>(region.outputWidth) *
                                     region.outputHeight * 4);
    std::vector<uint8_t> transformedRow(
        static_cast<size_t>(region.outputWidth) * 4);
    for (uint32_t outputY = 0; outputY < region.outputHeight; ++outputY) {
      const uint32_t sourceY = std::min<uint32_t>(
          height - 1, (static_cast<uint64_t>(outputY) * 2 + 1) * height /
                          (static_cast<uint64_t>(region.outputHeight) * 2));
      const size_t encodedRow =
          static_cast<size_t>(encodedRowForSource[sourceY]) * width;
      uint8_t* output = framePixels.data() +
                        static_cast<size_t>(outputY) * region.outputWidth * 4;
      for (uint32_t outputX = 0; outputX < region.outputWidth; ++outputX) {
        const uint32_t sourceX = std::min<uint32_t>(
            width - 1, (static_cast<uint64_t>(outputX) * 2 + 1) * width /
                           (static_cast<uint64_t>(region.outputWidth) * 2));
        const uint8_t index = indices[encodedRow + sourceX];
        if (index >= palette.size()) {
          throw std::runtime_error("GIF color index exceeds palette");
        }
        auto color = palette[index];
        if (static_cast<int32_t>(index) == pendingControl.transparentIndex) {
          color = {0, 0, 0, 0};
        }
        std::copy(color.begin(), color.end(), output + outputX * 4);
      }
      colorTransform.apply(output, transformedRow.data(), region.outputWidth);
      std::memcpy(output, transformedRow.data(), transformedRow.size());
    }

    const auto blend = IncrementalBlendOperationNative::Over;
    canvas->beginFrame(region, pendingControl.disposal);
    canvas->composite(region, framePixels.data(), blend);
    auto update = canvas->makeFrameUpdate(ImageFormat::Gif, publishedFrameCount,
                                          pendingControl.durationMillis, region,
                                          blend, pendingControl.disposal);
    sink(std::move(*update));
    canvas->disposeFrame(region, pendingControl.disposal);
    ++publishedFrameCount;
  }

  bool readSubBlocks(std::vector<uint8_t>& output) {
    size_t position = cursor;
    while (true) {
      if (position >= input.size()) {
        return false;
      }
      const size_t blockSize = input[position++];
      if (blockSize == 0) {
        cursor = position;
        return true;
      }
      if (input.size() - position < blockSize) {
        return false;
      }
      output.insert(output.end(), input.begin() + position,
                    input.begin() + position + blockSize);
      position += blockSize;
    }
  }

  std::vector<std::array<uint8_t, 4>> readPalette(const uint8_t* bytes,
                                                  size_t size) const {
    std::vector<std::array<uint8_t, 4>> palette(size);
    for (size_t index = 0; index < size; ++index) {
      palette[index] = {bytes[index * 3], bytes[index * 3 + 1],
                        bytes[index * 3 + 2], 255};
    }
    return palette;
  }

  IncrementalUpdate makeInfoUpdate(IncrementalUpdateType type) const {
    const auto& output = canvas->outputDimensions();
    IncrementalUpdate update{
        .type = type,
        .format = static_cast<int32_t>(ImageFormat::Gif),
        .capabilities = 0,
        .info = nullptr,
        .snapshot = nullptr,
        .animationFrame = nullptr,
    };
    update.info =
        std::make_unique<IncrementalImageInfoNative>(IncrementalImageInfoNative{
            .format = ImageFormat::Gif,
            .width = sourceWidth,
            .height = sourceHeight,
            .outputWidth = output.width,
            .outputHeight = output.height,
            .isAnimated = type == IncrementalUpdateType::Complete
                              ? publishedFrameCount > 1
                              : true,
            .hasAlpha = hasAlpha,
            .frameCount = type == IncrementalUpdateType::Complete
                              ? publishedFrameCount
                              : -1,
            .loopCount = loopCount,
        });
    return update;
  }

  IncrementalDecodeOptionsNative options;
  IncrementalColorTransform colorTransform;
  std::vector<uint8_t> input;
  std::vector<std::array<uint8_t, 4>> globalPalette;
  std::unique_ptr<IncrementalAnimationCanvas> canvas;
  GifGraphicControl pendingControl;
  size_t cursor = 0;
  uint32_t sourceWidth = 0;
  uint32_t sourceHeight = 0;
  int32_t loopCount = -1;
  int32_t publishedFrameCount = 0;
  bool headerParsed = false;
  bool complete = false;
  bool hasAlpha = false;
  bool resourceUnsupported = false;
};

} // namespace

std::unique_ptr<IncrementalFormatDecoder>
create_incremental_gif_decoder(const IncrementalDecodeOptionsNative& options) {
  return std::make_unique<IncrementalGifDecoder>(options);
}
