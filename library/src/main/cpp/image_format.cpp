#include "image_format.h"

#include <algorithm>
#include <iterator>

namespace {

constexpr size_t kDefaultSniffBytes = 32;
constexpr size_t kMaximumFtypSniffBytes = 4096;

bool has_bytes(const uint8_t* data, size_t size, size_t required) {
  return data != nullptr && size >= required;
}

uint32_t read_big_endian_u32(const uint8_t* data) {
  return static_cast<uint32_t>(data[0]) << 24 |
         static_cast<uint32_t>(data[1]) << 16 |
         static_cast<uint32_t>(data[2]) << 8 | static_cast<uint32_t>(data[3]);
}

bool is_ftyp(const uint8_t* data, size_t size) {
  return has_bytes(data, size, 8) && data[4] == 'f' && data[5] == 't' &&
         data[6] == 'y' && data[7] == 'p';
}

ImageFormatDetectionResult detect_ftyp(const uint8_t* data, size_t size,
                                       bool endOfInput) {
  if (!is_ftyp(data, size)) {
    return {ImageFormatDetectionState::Unsupported, ImageFormat::Heif};
  }
  if (size < 12) {
    return {endOfInput ? ImageFormatDetectionState::Unsupported
                       : ImageFormatDetectionState::NeedMoreData,
            ImageFormat::Heif};
  }

  const uint32_t boxSize = read_big_endian_u32(data);
  if (boxSize < 12) {
    return {ImageFormatDetectionState::Unsupported, ImageFormat::Heif};
  }

  const size_t availableBoxBytes = std::min<size_t>(boxSize, size);
  const auto matches_known_brand = [](const uint8_t* brand) {
    return (brand[0] == 'h' && brand[1] == 'e' &&
            (brand[2] == 'i' || brand[2] == 'v')) ||
           (brand[0] == 'a' && brand[1] == 'v' && brand[2] == 'i');
  };

  if (matches_known_brand(data + 8)) {
    const auto* brand = data + 8;
    return {ImageFormatDetectionState::Detected,
            brand[0] == 'a' ? ImageFormat::Avif : ImageFormat::Heif};
  }

  // Bytes 12..15 contain the minor version, not a compatible brand.
  for (size_t offset = 16; offset + 4 <= availableBoxBytes; offset += 4) {
    const auto* brand = data + offset;
    if (matches_known_brand(brand)) {
      return {ImageFormatDetectionState::Detected,
              brand[0] == 'a' ? ImageFormat::Avif : ImageFormat::Heif};
    }
  }

  const size_t sniffLimit = std::min<size_t>(boxSize, kMaximumFtypSniffBytes);
  if (!endOfInput && size < sniffLimit) {
    return {ImageFormatDetectionState::NeedMoreData, ImageFormat::Heif};
  }
  return {ImageFormatDetectionState::Unsupported, ImageFormat::Heif};
}

} // namespace

bool is_jpeg(const uint8_t* data, size_t size) {
  return has_bytes(data, size, 3) && data[0] == 0xFF && data[1] == 0xD8 &&
         data[2] == 0xFF;
}

bool is_png(const uint8_t* data, size_t size) {
  static constexpr uint8_t signature[] = {0x89, 'P',  'N',  'G',
                                          0x0D, 0x0A, 0x1A, 0x0A};
  return has_bytes(data, size, sizeof(signature)) &&
         std::equal(std::begin(signature), std::end(signature), data);
}

bool is_webp(const uint8_t* data, size_t size) {
  return has_bytes(data, size, 12) && data[0] == 'R' && data[1] == 'I' &&
         data[2] == 'F' && data[3] == 'F' && data[8] == 'W' && data[9] == 'E' &&
         data[10] == 'B' && data[11] == 'P';
}

bool is_gif(const uint8_t* data, size_t size) {
  return has_bytes(data, size, 6) && data[0] == 'G' && data[1] == 'I' &&
         data[2] == 'F' && data[3] == '8' &&
         ((data[4] == '7' && data[5] == 'a') ||
          (data[4] == '9' && data[5] == 'a'));
}

bool is_heif(const uint8_t* data, size_t size) {
  const auto result = detect_ftyp(data, size, true);
  return result.state == ImageFormatDetectionState::Detected &&
         result.format == ImageFormat::Heif;
}

bool is_avif(const uint8_t* data, size_t size) {
  const auto result = detect_ftyp(data, size, true);
  return result.state == ImageFormatDetectionState::Detected &&
         result.format == ImageFormat::Avif;
}

bool is_jxl(const uint8_t* data, size_t size) {
  const bool container = has_bytes(data, size, 12) && data[0] == 0 &&
                         data[1] == 0 && data[2] == 0 && data[3] == 0x0C &&
                         data[4] == 'J' && data[5] == 'X' && data[6] == 'L' &&
                         data[7] == ' ' && data[8] == 0x0D && data[9] == 0x0A &&
                         data[10] == 0x87 && data[11] == 0x0A;
  const bool codestream =
      has_bytes(data, size, 2) && data[0] == 0xFF && data[1] == 0x0A;
  return container || codestream;
}

ImageFormatDetectionResult detect_image_format(const uint8_t* data, size_t size,
                                               bool endOfInput) {
  if (is_jpeg(data, size)) {
    return {ImageFormatDetectionState::Detected, ImageFormat::Jpeg};
  }
  if (is_png(data, size)) {
    return {ImageFormatDetectionState::Detected, ImageFormat::Png};
  }
  if (is_webp(data, size)) {
    return {ImageFormatDetectionState::Detected, ImageFormat::Webp};
  }
  if (is_gif(data, size)) {
    return {ImageFormatDetectionState::Detected, ImageFormat::Gif};
  }
  if (is_jxl(data, size)) {
    return {ImageFormatDetectionState::Detected, ImageFormat::Jxl};
  }
  if (is_ftyp(data, size)) {
    return detect_ftyp(data, size, endOfInput);
  }

  if (!endOfInput && size < kDefaultSniffBytes) {
    return {ImageFormatDetectionState::NeedMoreData, ImageFormat::Jpeg};
  }
  return {ImageFormatDetectionState::Unsupported, ImageFormat::Jpeg};
}
