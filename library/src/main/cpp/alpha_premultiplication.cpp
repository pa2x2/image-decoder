#include "alpha_premultiplication.h"

#include <algorithm>

void premultiply_rgba(const uint8_t* straight, uint8_t* premultiplied,
                      size_t pixelCount) {
  for (size_t pixel = 0; pixel < pixelCount; ++pixel) {
    const uint32_t alpha = straight[3];
    if (alpha == 255) {
      if (premultiplied != straight) {
        std::copy(straight, straight + 4, premultiplied);
      }
    } else {
      for (int component = 0; component < 3; ++component) {
        premultiplied[component] =
            static_cast<uint8_t>((straight[component] * alpha + 127) / 255);
      }
      premultiplied[3] = static_cast<uint8_t>(alpha);
    }
    straight += 4;
    premultiplied += 4;
  }
}

void unpremultiply_rgba(uint8_t* pixels, size_t pixelCount) {
  for (size_t pixel = 0; pixel < pixelCount; ++pixel) {
    const uint32_t alpha = pixels[3];
    if (alpha != 255) {
      for (int component = 0; component < 3; ++component) {
        pixels[component] =
            alpha == 0
                ? 0
                : static_cast<uint8_t>(std::min<uint32_t>(
                      255, (pixels[component] * 255 + alpha / 2) / alpha));
      }
    }
    pixels += 4;
  }
}
