#ifndef IMAGEDECODER_INCREMENTAL_TEST_WEBP_FIXTURE_H
#define IMAGEDECODER_INCREMENTAL_TEST_WEBP_FIXTURE_H

#include "fixtures/encoded_image_fixture.h"

#include <cstdint>
#include <vector>

EncodedImageFixture make_webp_fixture(bool lossless);
std::vector<uint8_t> make_animated_webp_fallback_fixture();

#endif // IMAGEDECODER_INCREMENTAL_TEST_WEBP_FIXTURE_H
