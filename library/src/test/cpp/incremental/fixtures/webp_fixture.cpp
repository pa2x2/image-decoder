#include "fixtures/webp_fixture.h"

#include "fixtures/image_pattern.h"
#include "support/decode_harness.h"

#include <webp/decode.h>
#include <webp/encode.h>

#include <array>
#include <cstddef>
#include <utility>
#include <vector>

namespace {

void write_little_endian_u32(uint8_t* output, uint32_t value) {
  output[0] = static_cast<uint8_t>(value);
  output[1] = static_cast<uint8_t>(value >> 8);
  output[2] = static_cast<uint8_t>(value >> 16);
  output[3] = static_cast<uint8_t>(value >> 24);
}

std::vector<uint8_t> encode_webp(const std::vector<uint8_t>& rgba,
                                 bool lossless) {
  uint8_t* encodedBytes = nullptr;
  const size_t encodedSize =
      lossless
          ? WebPEncodeLosslessRGBA(rgba.data(), kFixtureWidth, kFixtureHeight,
                                   kFixtureWidth * 4, &encodedBytes)
          : WebPEncodeRGBA(rgba.data(), kFixtureWidth, kFixtureHeight,
                           kFixtureWidth * 4, 90.0F, &encodedBytes);
  require_condition(encodedSize > 0 && encodedBytes != nullptr,
                    "WebP fixture must encode successfully");
  std::vector<uint8_t> output(encodedBytes, encodedBytes + encodedSize);
  WebPFree(encodedBytes);
  return output;
}

std::vector<uint8_t>
decode_webp_reference(const std::vector<uint8_t>& encoded) {
  int width = 0;
  int height = 0;
  uint8_t* decoded =
      WebPDecodeRGBA(encoded.data(), encoded.size(), &width, &height);
  require_condition(decoded != nullptr && width == kFixtureWidth &&
                        height == kFixtureHeight,
                    "WebP fixture must decode at its encoded dimensions");
  std::vector<uint8_t> output(decoded, decoded + static_cast<size_t>(width) *
                                                     height * 4);
  WebPFree(decoded);
  return output;
}

} // namespace

EncodedImageFixture make_webp_fixture(bool lossless) {
  auto source = make_fixture_pattern(true);
  auto encoded = encode_webp(source, lossless);
  return EncodedImageFixture{
      .width = kFixtureWidth,
      .height = kFixtureHeight,
      .expectedRgba = decode_webp_reference(encoded),
      .encoded = std::move(encoded),
  };
}

std::vector<uint8_t> make_animated_webp_fallback_fixture() {
  auto webp = make_webp_fixture(false).encoded;
  require_condition(webp.size() >= 12,
                    "WebP fixture must contain a RIFF header");
  const std::array<uint8_t, 14> animationChunk = {
      'A', 'N', 'I', 'M', 6, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  };
  webp.insert(webp.begin() + 12, animationChunk.begin(), animationChunk.end());
  write_little_endian_u32(webp.data() + 4,
                          static_cast<uint32_t>(webp.size() - 8));
  return webp;
}
