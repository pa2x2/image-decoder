#ifndef IMAGEDECODER_INCREMENTAL_TEST_SCALED_DECODE_EXPECTATIONS_H
#define IMAGEDECODER_INCREMENTAL_TEST_SCALED_DECODE_EXPECTATIONS_H

#include "support/decode_harness.h"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

// Decodes `encoded` at full size and at `outputWidth`, and requires the scaled decode to publish the area average of the
// full-size one. The averaging arithmetic itself is covered by the area
// resampling tests.
void require_scaled_still_is_area_average(
    const IncrementalDecoderFactory& factory,
    const std::vector<uint8_t>& encoded, uint32_t width, uint32_t height,
    uint32_t outputWidth, size_t chunkSize, std::string_view description);

void require_scaled_frames_are_area_averages(
    const IncrementalDecoderFactory& factory,
    const std::vector<uint8_t>& encoded, uint32_t width, uint32_t height,
    uint32_t outputWidth, size_t chunkSize, std::string_view description);

#endif // IMAGEDECODER_INCREMENTAL_TEST_SCALED_DECODE_EXPECTATIONS_H
