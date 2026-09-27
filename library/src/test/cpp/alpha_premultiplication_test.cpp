#include "alpha_premultiplication.h"
#include "support/decode_harness.h"

#include <vector>

namespace {

void require_premultiplying_rounds_colors_by_alpha() {
  // 255 * 128 / 255 must stay 128, and 1 * 128 / 255 = 0.502 rounds up.
  std::vector<uint8_t> pixels = {255, 1,  3,   128, 200, 100, 50, 255,
                                 90,  40, 250, 0,   30,  60,  90, 51};
  premultiply_rgba(pixels.data(), pixels.data(), pixels.size() / 4);
  require_condition(pixels == std::vector<uint8_t>{128, 1, 2, 128, 200, 100, 50,
                                                   255, 0, 0, 0, 0, 6, 12, 18,
                                                   51},
                    "premultiplying must round each color times its alpha");
}

void require_premultiplied_copy_leaves_straight_source() {
  const std::vector<uint8_t> straight = {100, 150, 200, 128};
  std::vector<uint8_t> source = straight;
  std::vector<uint8_t> premultiplied(4);
  premultiply_rgba(source.data(), premultiplied.data(), 1);
  require_condition(premultiplied == std::vector<uint8_t>{50, 75, 100, 128} &&
                        source == straight,
                    "a premultiplied copy must not change its straight source");
}

void require_unpremultiplying_rounds_and_clamps_colors() {
  // 1 * 255 / 128 = 1.99 rounds up; colors above their alpha are invalid and
  // clamp to white.
  std::vector<uint8_t> pixels = {64, 32, 1,  128, 200, 100, 50, 255,
                                 90, 40, 25, 0,   200, 0,   0,  100};
  unpremultiply_rgba(pixels.data(), pixels.size() / 4);
  require_condition(pixels == std::vector<uint8_t>{128, 64, 2, 128, 200, 100,
                                                   50, 255, 0, 0, 0, 0, 255, 0,
                                                   0, 100},
                    "unpremultiplying must round each color over its alpha");
}

} // namespace

void run_alpha_premultiplication_tests() {
  require_premultiplying_rounds_colors_by_alpha();
  require_premultiplied_copy_leaves_straight_source();
  require_unpremultiplying_rounds_and_clamps_colors();
}
