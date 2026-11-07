#include "decoder_png.h"
#include "fixtures/png_fixture.h"
#include "support/decode_harness.h"

#include <memory>
#include <string>

namespace {

std::string describe(const Rect& rect) {
  return std::to_string(rect.x) + "," + std::to_string(rect.y) + " " +
         std::to_string(rect.width) + "x" + std::to_string(rect.height);
}

// Border detection reads the image as gray, so it must drop the alpha channel
// that a tRNS chunk adds to a grayscale image, not read it as more gray.
void require_transparent_gray_png_borders_are_found(bool adam7) {
  auto fixture = make_bordered_transparent_gray_png_fixture(adam7);
  PngDecoder decoder(
      std::make_shared<Stream>(fixture.encoded.data(),
                               static_cast<uint32_t>(fixture.encoded.size())),
      true, cmsCreate_sRGBProfile());

  const Rect& found = decoder.info.bounds;
  const Rect& expected = fixture.content;
  require_condition(
      found.x == expected.x && found.y == expected.y &&
          found.width == expected.width && found.height == expected.height,
      std::string(adam7 ? "interlaced" : "progressive") +
          " gray PNG with tRNS must crop to " + describe(expected) +
          ", but crops to " + describe(found));
}

} // namespace

void run_png_border_detection_tests() {
  require_transparent_gray_png_borders_are_found(false);
  require_transparent_gray_png_borders_are_found(true);
}
