#include "decoder_jxl.h"
#include "fixtures/jxl_fixture.h"
#include "support/decode_harness.h"

#include <memory>
#include <string>
#include <vector>

namespace {

// Decoders hand straight alpha to color management, averaging and the bitmap
// conversion, which premultiplies it exactly once. An image stored
// premultiplied must therefore come out straight.
void require_premultiplied_jxl_decodes_to_straight_alpha() {
  auto fixture = make_premultiplied_alpha_jxl_fixture();
  JpegxlDecoder decoder(
      std::make_shared<Stream>(fixture.encoded.data(),
                               static_cast<uint32_t>(fixture.encoded.size())),
      false, cmsCreate_sRGBProfile());
  const Rect region = {
      .x = 0, .y = 0, .width = fixture.width, .height = fixture.height};
  std::vector<uint8_t> pixels(fixture.expectedRgba.size());
  decoder.decode(pixels.data(), region, region, 1);

  // The stored colors were rounded after premultiplying, which dividing by the
  // smallest alpha of the fixture, 64, magnifies to at most 2 levels.
  const uint8_t error = maximum_channel_error(pixels, fixture.expectedRgba);
  require_condition(error <= 2, "premultiplied JXL must decode to straight "
                                "alpha, but differs by " +
                                    std::to_string(error));
}

} // namespace

void run_straight_alpha_decode_tests() {
  require_premultiplied_jxl_decodes_to_straight_alpha();
}
