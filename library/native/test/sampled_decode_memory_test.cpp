#include "decoder_png.h"
#include "fixtures/png_fixture.h"
#include "support/decode_harness.h"
#include "support/peak_allocation.h"

#include <memory>
#include <string>
#include <vector>

namespace {

// Every pass of an Adam7 image spans the whole image, so the blocks of a
// sampled decode only complete with the last pass. Holding the source rows of
// the region until then multiplies memory by the square of the sample size;
// the decode must instead only keep what the output blocks need.
void require_sampled_adam7_png_does_not_hold_the_region() {
  constexpr uint32_t sampleSize = 8;
  constexpr uint32_t components = 4;
  auto encoded = make_png_fixture(true).encoded;
  PngDecoder decoder(std::make_shared<Stream>(
                         encoded.data(), static_cast<uint32_t>(encoded.size())),
                     false, cmsCreate_sRGBProfile());
  Rect inRect = {.x = 0,
                 .y = 0,
                 .width = decoder.info.imageWidth,
                 .height = decoder.info.imageHeight};
  const Rect outRect = inRect.downsample(sampleSize);
  std::vector<uint8_t> output(static_cast<size_t>(outRect.width) *
                              outRect.height * components);

  const size_t peak = peak_allocated_bytes_during(
      [&] { decoder.decode(output.data(), outRect, inRect, sampleSize); });

  const size_t regionBytes =
      static_cast<size_t>(inRect.width) * inRect.height * components;
  require_condition(peak < regionBytes / 4,
                    "sampled Adam7 PNG decode allocated " +
                        std::to_string(peak) + " bytes for a region of " +
                        std::to_string(regionBytes) + " bytes");
}

} // namespace

void run_sampled_decode_memory_tests() {
  require_sampled_adam7_png_does_not_hold_the_region();
}
