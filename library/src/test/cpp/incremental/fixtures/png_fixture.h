#ifndef IMAGEDECODER_INCREMENTAL_TEST_PNG_FIXTURE_H
#define IMAGEDECODER_INCREMENTAL_TEST_PNG_FIXTURE_H

#include "fixtures/encoded_image_fixture.h"

#include <cstdint>
#include <vector>

struct AnimatedPngFixture {
  uint32_t width;
  uint32_t height;
  int32_t loopCount;
  std::vector<uint64_t> durationsMillis;
  std::vector<std::vector<uint8_t>> expectedFrames;
  std::vector<uint8_t> encoded;
};

EncodedImageFixture make_png_fixture(bool adam7);
// Two-bit grayscale without alpha, which decoders expand to RGBA.
EncodedImageFixture make_packed_gray_png_fixture(bool adam7);
AnimatedPngFixture make_animated_png_fixture();

#endif // IMAGEDECODER_INCREMENTAL_TEST_PNG_FIXTURE_H
