#ifndef IMAGEDECODER_IMAGE_FORMAT_H
#define IMAGEDECODER_IMAGE_FORMAT_H

#include <cstddef>
#include <cstdint>

enum class ImageFormat : int32_t {
  Jpeg = 0,
  Png = 1,
  Webp = 2,
  Gif = 3,
  Heif = 4,
  Avif = 5,
  Jxl = 6,
};

enum class ImageFormatDetectionState {
  NeedMoreData,
  Detected,
  Unsupported,
};

struct ImageFormatDetectionResult {
  ImageFormatDetectionState state;
  ImageFormat format;
};

ImageFormatDetectionResult detect_image_format(const uint8_t* data, size_t size,
                                               bool endOfInput);

bool is_jpeg(const uint8_t* data, size_t size);
bool is_png(const uint8_t* data, size_t size);
bool is_webp(const uint8_t* data, size_t size);
bool is_gif(const uint8_t* data, size_t size);
bool is_heif(const uint8_t* data, size_t size);
bool is_avif(const uint8_t* data, size_t size);
bool is_jxl(const uint8_t* data, size_t size);

#endif // IMAGEDECODER_IMAGE_FORMAT_H
