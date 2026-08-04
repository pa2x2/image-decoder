#ifndef IMAGEDECODER_INCREMENTAL_TEST_PNG_FIXTURE_H
#define IMAGEDECODER_INCREMENTAL_TEST_PNG_FIXTURE_H

#include "fixtures/encoded_image_fixture.h"

#include <cstdint>
#include <vector>

EncodedImageFixture make_png_fixture(bool adam7);
std::vector<uint8_t> make_apng_fallback_fixture();

#endif // IMAGEDECODER_INCREMENTAL_TEST_PNG_FIXTURE_H
