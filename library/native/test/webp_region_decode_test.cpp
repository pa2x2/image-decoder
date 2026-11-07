#include "decoder_webp.h"
#include "fixtures/image_pattern.h"
#include "fixtures/webp_fixture.h"
#include "support/decode_harness.h"

#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {

std::string describe(const Rect& rect) {
  return std::to_string(rect.x) + "," + std::to_string(rect.y) + " " +
         std::to_string(rect.width) + "x" + std::to_string(rect.height);
}

// A region decoded at full size must hold exactly the pixels it covers in the
// whole image, wherever it starts and whichever edges it shares with the image.
void require_regions_match_whole_image(bool lossless) {
  auto fixture = make_webp_fixture(lossless);
  const Rect regions[] = {
      {.x = 32, .y = 64, .width = 96, .height = 128},
      {.x = 33, .y = 65, .width = 95, .height = 127},
      {.x = 0, .y = 0, .width = 61, .height = 77},
      {.x = 131,
       .y = 307,
       .width = kFixtureWidth - 131,
       .height = kFixtureHeight - 307},
  };
  for (const Rect& region : regions) {
    WebpDecoder decoder(
        std::make_shared<Stream>(fixture.encoded.data(),
                                 static_cast<uint32_t>(fixture.encoded.size())),
        false, cmsCreate_sRGBProfile());
    const size_t stride = static_cast<size_t>(region.width) * 4;
    std::vector<uint8_t> pixels(stride * region.height);
    decoder.decode(pixels.data(), region, region, 1);

    std::vector<uint8_t> expected(pixels.size());
    for (uint32_t row = 0; row < region.height; ++row) {
      memcpy(expected.data() + row * stride,
             fixture.expectedRgba.data() +
                 ((static_cast<size_t>(region.y) + row) * kFixtureWidth +
                  region.x) *
                     4,
             stride);
    }
    require_condition(pixels == expected,
                      std::string(lossless ? "lossless" : "lossy") +
                          " WebP region " + describe(region) +
                          " must equal the same pixels of the whole image");
  }
}

} // namespace

void run_webp_region_decode_tests() {
  require_regions_match_whole_image(false);
  require_regions_match_whole_image(true);
}
