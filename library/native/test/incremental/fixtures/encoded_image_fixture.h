#ifndef IMAGEDECODER_INCREMENTAL_TEST_ENCODED_IMAGE_FIXTURE_H
#define IMAGEDECODER_INCREMENTAL_TEST_ENCODED_IMAGE_FIXTURE_H

#include <cstdint>
#include <vector>

struct EncodedImageFixture {
  uint32_t width;
  uint32_t height;
  std::vector<uint8_t> expectedRgba;
  std::vector<uint8_t> encoded;
};

#endif // IMAGEDECODER_INCREMENTAL_TEST_ENCODED_IMAGE_FIXTURE_H
