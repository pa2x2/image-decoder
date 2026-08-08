#include "fixtures/webp_fixture.h"

#include "fixtures/image_pattern.h"
#include "support/decode_harness.h"

#include <webp/decode.h>
#include <webp/encode.h>
#include <webp/mux.h>

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

namespace {

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

AnimatedWebpFixture make_animated_webp_fixture() {
  constexpr int32_t loopCount = 2;
  const std::vector<uint64_t> durationsMillis = {90, 160};
  std::vector<std::vector<uint8_t>> frames;
  frames.push_back(make_fixture_pattern(true));
  auto secondFrame = frames.front();
  for (size_t offset = 0; offset < secondFrame.size(); offset += 4) {
    std::swap(secondFrame[offset], secondFrame[offset + 1]);
  }
  frames.push_back(std::move(secondFrame));

  WebPAnimEncoderOptions options;
  require_condition(WebPAnimEncoderOptionsInit(&options) != 0,
                    "animated WebP options must initialize");
  options.anim_params.loop_count = loopCount;
  auto encoder =
      std::unique_ptr<WebPAnimEncoder, decltype(&WebPAnimEncoderDelete)>(
          WebPAnimEncoderNew(kFixtureWidth, kFixtureHeight, &options),
          WebPAnimEncoderDelete);
  require_condition(encoder != nullptr,
                    "animated WebP encoder must be created");

  WebPConfig config;
  require_condition(WebPConfigInit(&config) != 0,
                    "animated WebP config must initialize");
  config.lossless = 1;
  config.quality = 100.0F;
  require_condition(WebPValidateConfig(&config) != 0,
                    "animated WebP config must be valid");

  int timestamp = 0;
  for (size_t index = 0; index < frames.size(); ++index) {
    WebPPicture picture;
    require_condition(WebPPictureInit(&picture) != 0,
                      "animated WebP picture must initialize");
    picture.use_argb = 1;
    picture.width = kFixtureWidth;
    picture.height = kFixtureHeight;
    require_condition(WebPPictureImportRGBA(&picture, frames[index].data(),
                                            kFixtureWidth * 4) != 0,
                      "animated WebP pixels must import");
    const bool added =
        WebPAnimEncoderAdd(encoder.get(), &picture, timestamp, &config) != 0;
    WebPPictureFree(&picture);
    require_condition(added, "animated WebP frame must encode");
    timestamp += static_cast<int>(durationsMillis[index]);
  }
  require_condition(
      WebPAnimEncoderAdd(encoder.get(), nullptr, timestamp, nullptr) != 0,
      "animated WebP timeline must close");
  WebPData encodedData;
  WebPDataInit(&encodedData);
  require_condition(WebPAnimEncoderAssemble(encoder.get(), &encodedData) != 0,
                    "animated WebP must assemble");
  std::vector<uint8_t> encoded(encodedData.bytes,
                               encodedData.bytes + encodedData.size);
  WebPDataClear(&encodedData);
  return AnimatedWebpFixture{
      .width = kFixtureWidth,
      .height = kFixtureHeight,
      .loopCount = loopCount,
      .durationsMillis = durationsMillis,
      .expectedFrames = std::move(frames),
      .encoded = std::move(encoded),
  };
}
