#ifndef IMAGEDECODER_INCREMENTAL_TEST_IMAGE_PATTERN_H
#define IMAGEDECODER_INCREMENTAL_TEST_IMAGE_PATTERN_H

#include <cstdint>
#include <vector>

constexpr uint32_t kFixtureWidth = 192;
constexpr uint32_t kFixtureHeight = 384;

std::vector<uint8_t> make_fixture_pattern(bool alpha);

#endif // IMAGEDECODER_INCREMENTAL_TEST_IMAGE_PATTERN_H
