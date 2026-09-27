#include "box_downsampler.h"
#include "decoder_jpeg.h"
#include "decoder_jxl.h"
#include "decoder_png.h"
#include "fixtures/jpeg_fixture.h"
#include "fixtures/jxl_fixture.h"
#include "fixtures/png_fixture.h"
#include "support/decode_harness.h"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

using DecoderFactory = std::function<std::unique_ptr<BaseDecoder>(
    std::shared_ptr<Stream>&&)>;

// Offsets are multiples of the largest sample size, so the coarse and fine
// decodes start their blocks at the same source pixel.
constexpr Rect kRegion = {.x = 32, .y = 64, .width = 128, .height = 256};

// Every fixture decodes to four components before color management.
constexpr uint32_t kComponents = 4;

std::vector<uint8_t> decode_region(const DecoderFactory& factory,
                                   std::vector<uint8_t>& encoded,
                                   uint32_t sampleSize) {
  auto decoder = factory(std::make_shared<Stream>(
      encoded.data(), static_cast<uint32_t>(encoded.size())));
  Rect inRect = kRegion;
  const Rect outRect = inRect.downsample(sampleSize);
  std::vector<uint8_t> pixels(static_cast<size_t>(outRect.width) *
                              outRect.height * kComponents);
  decoder->decode(pixels.data(), outRect, inRect, sampleSize);
  return pixels;
}

std::vector<uint8_t> box_downsampled(const std::vector<uint8_t>& pixels,
                                     uint32_t width, uint32_t height,
                                     uint32_t factor) {
  const uint32_t outputWidth = width / factor;
  BoxDownsampler downsampler(outputWidth, factor, kComponents, true);
  std::vector<uint8_t> output(static_cast<size_t>(outputWidth) *
                              (height / factor) * kComponents);
  for (uint32_t outputY = 0; outputY < height / factor; ++outputY) {
    for (uint32_t row = 0; row < factor; ++row) {
      downsampler.addRow(pixels.data() + (static_cast<size_t>(outputY) *
                                              factor +
                                          row) *
                                             width * kComponents);
    }
    downsampler.writeRow(output.data() +
                         static_cast<size_t>(outputY) * outputWidth *
                             kComponents);
  }
  return output;
}

// A decode at `sampleSize` must equal the box average of the same region
// decoded at the finer `referenceSampleSize`. The averaging itself is covered
// by the box downsampler tests.
void require_sampled_decode_averages_blocks(const DecoderFactory& factory,
                                            std::vector<uint8_t> encoded,
                                            uint32_t sampleSize,
                                            uint32_t referenceSampleSize,
                                            std::string_view description) {
  Rect region = kRegion;
  const Rect reference = region.downsample(referenceSampleSize);
  const auto expected =
      box_downsampled(decode_region(factory, encoded, referenceSampleSize),
                      reference.width, reference.height,
                      sampleSize / referenceSampleSize);
  require_condition(decode_region(factory, encoded, sampleSize) == expected,
                    std::string(description) +
                        " must average every pixel of each sampled block");
}

std::unique_ptr<BaseDecoder> make_png(std::shared_ptr<Stream>&& stream) {
  return std::make_unique<PngDecoder>(std::move(stream), false,
                                      cmsCreate_sRGBProfile());
}

std::unique_ptr<BaseDecoder> make_jpeg(std::shared_ptr<Stream>&& stream) {
  return std::make_unique<JpegDecoder>(std::move(stream), false,
                                       cmsCreate_sRGBProfile());
}

std::unique_ptr<BaseDecoder> make_jxl(std::shared_ptr<Stream>&& stream) {
  return std::make_unique<JpegxlDecoder>(std::move(stream), false,
                                         cmsCreate_sRGBProfile());
}

} // namespace

void run_sampled_decode_tests() {
  require_sampled_decode_averages_blocks(
      make_png, make_png_fixture(false).encoded, 4, 1, "sampled PNG");
  require_sampled_decode_averages_blocks(
      make_png, make_png_fixture(true).encoded, 4, 1, "sampled Adam7 PNG");
  // libjpeg scales by up to 8 itself; the decoder averages the rest.
  require_sampled_decode_averages_blocks(
      make_jpeg, make_jpeg_fixture(false).encoded, 32, 8, "sampled JPEG");
  require_sampled_decode_averages_blocks(
      make_jxl, make_jxl_still_fixture(false).encoded, 4, 1, "sampled JXL");
}
