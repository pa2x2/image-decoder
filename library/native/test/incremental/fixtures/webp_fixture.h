#ifndef IMAGEDECODER_INCREMENTAL_TEST_WEBP_FIXTURE_H
#define IMAGEDECODER_INCREMENTAL_TEST_WEBP_FIXTURE_H

#include "fixtures/encoded_image_fixture.h"

#include <cstdint>
#include <vector>

struct AnimatedWebpFixture {
  uint32_t width;
  uint32_t height;
  int32_t loopCount;
  std::vector<uint64_t> durationsMillis;
  std::vector<std::vector<uint8_t>> expectedFrames;
  std::vector<uint8_t> encoded;
};

EncodedImageFixture make_webp_fixture(bool lossless);
AnimatedWebpFixture make_animated_webp_fixture();

#endif // IMAGEDECODER_INCREMENTAL_TEST_WEBP_FIXTURE_H
