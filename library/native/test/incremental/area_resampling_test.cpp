#include "incremental/area_resampling.h"
#include "support/decode_harness.h"

#include <algorithm>
#include <vector>

namespace {

void require_fractional_footprints_weigh_partial_pixels() {
  // A lone white pixel in the middle of 3x3 black lies half in each of the
  // 2x2 output pixels along both axes: a quarter of a pixel in a 1.5x1.5
  // footprint, so each output is 255 * 0.25 / 2.25.
  std::vector<uint8_t> source(3 * 3 * 4, 0);
  for (size_t offset = 3; offset < source.size(); offset += 4) {
    source[offset] = 255;
  }
  std::fill_n(source.begin() + (1 * 3 + 1) * 4, 3, 255);
  std::vector<uint8_t> output(2 * 2 * 4);
  resample_area_rect(source.data(), AreaAxis(3, 2), AreaAxis(3, 2),
                     AreaOutputRect{.left = 0, .top = 0, .right = 2, .bottom = 2},
                     output.data());
  for (size_t offset = 0; offset < output.size(); offset += 4) {
    require_condition(output[offset] == 28 && output[offset + 1] == 28 &&
                          output[offset + 2] == 28 && output[offset + 3] == 255,
                      "area resampling must weigh pixels by covered area");
  }
}

void require_transparent_pixels_do_not_tint_neighbours() {
  const std::vector<uint8_t> source = {255, 0, 0, 255, 0, 0, 255, 0};
  std::vector<uint8_t> output(4);
  resample_area_rect(source.data(), AreaAxis(2, 1), AreaAxis(1, 1),
                     AreaOutputRect{.left = 0, .top = 0, .right = 1, .bottom = 1},
                     output.data());
  require_condition(output == std::vector<uint8_t>{255, 0, 0, 128},
                    "area resampling must average color by alpha coverage");
}

} // namespace

void run_area_resampling_tests() {
  require_fractional_footprints_weigh_partial_pixels();
  require_transparent_pixels_do_not_tint_neighbours();
}
