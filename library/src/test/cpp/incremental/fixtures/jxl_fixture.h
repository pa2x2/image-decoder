#ifndef IMAGEDECODER_INCREMENTAL_TEST_JXL_FIXTURE_H
#define IMAGEDECODER_INCREMENTAL_TEST_JXL_FIXTURE_H

#include "fixtures/encoded_image_fixture.h"

#include <cstdint>
#include <vector>

struct AnimatedJxlFixture {
  uint32_t width;
  uint32_t height;
  int32_t loopCount;
  std::vector<uint64_t> durationsMillis;
  std::vector<std::vector<uint8_t>> expectedFrames;
  std::vector<uint8_t> encoded;
};

EncodedImageFixture make_jxl_still_fixture(bool progressive);
EncodedImageFixture make_large_jxl_benchmark_fixture();
AnimatedJxlFixture make_animated_jxl_fixture();

#endif // IMAGEDECODER_INCREMENTAL_TEST_JXL_FIXTURE_H
