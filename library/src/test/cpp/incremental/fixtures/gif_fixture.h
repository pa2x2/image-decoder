#ifndef IMAGEDECODER_INCREMENTAL_TEST_GIF_FIXTURE_H
#define IMAGEDECODER_INCREMENTAL_TEST_GIF_FIXTURE_H

#include <cstdint>
#include <vector>

struct AnimatedGifFixture {
  uint32_t width;
  uint32_t height;
  int32_t loopCount;
  std::vector<uint64_t> durationsMillis;
  std::vector<std::vector<uint8_t>> expectedFrames;
  std::vector<uint8_t> encoded;
};

AnimatedGifFixture make_animated_gif_fixture();

#endif // IMAGEDECODER_INCREMENTAL_TEST_GIF_FIXTURE_H
