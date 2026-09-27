#include "fixtures/jxl_fixture.h"

#include "fixtures/image_pattern.h"
#include "support/decode_harness.h"

#include <jxl/decode.h>
#include <jxl/decode_cxx.h>
#include <jxl/encode.h>
#include <jxl/encode_cxx.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

namespace {

std::vector<uint8_t> make_pattern(uint32_t width, uint32_t height,
                                  uint32_t seed, bool alpha) {
  std::vector<uint8_t> rgba(static_cast<size_t>(width) * height * 4);
  for (uint32_t y = 0; y < height; ++y) {
    for (uint32_t x = 0; x < width; ++x) {
      const size_t offset = (static_cast<size_t>(y) * width + x) * 4;
      rgba[offset] = static_cast<uint8_t>(
          (x * 13 + y * 7 + (x * y + seed * 31) % 251) & 0xFF);
      rgba[offset + 1] =
          static_cast<uint8_t>((x * 3 + y * 17 + (x ^ y ^ seed) * 5) & 0xFF);
      rgba[offset + 2] = static_cast<uint8_t>(
          (x * 19 + y * 11 + (x * 5 ^ y * 9 ^ seed * 23)) & 0xFF);
      rgba[offset + 3] =
          alpha ? static_cast<uint8_t>(64 + (x * 11 + y * 17 + seed) % 192)
                : 255;
    }
  }
  return rgba;
}

void require_encoder_success(JxlEncoderStatus status,
                             std::string_view operation) {
  require_condition(status == JXL_ENC_SUCCESS, operation);
}

std::vector<uint8_t> decode_jxl_reference(const std::vector<uint8_t>& encoded,
                                          uint32_t expectedWidth,
                                          uint32_t expectedHeight) {
  auto decoder = JxlDecoderMake(nullptr);
  require_condition(decoder != nullptr,
                    "JXL reference decoder must be created");
  require_condition(
      JxlDecoderSubscribeEvents(decoder.get(),
                                JXL_DEC_BASIC_INFO | JXL_DEC_FULL_IMAGE) ==
          JXL_DEC_SUCCESS,
      "JXL reference events must be accepted");
  require_condition(JxlDecoderSetInput(decoder.get(), encoded.data(),
                                       encoded.size()) == JXL_DEC_SUCCESS,
                    "JXL reference input must be accepted");
  JxlDecoderCloseInput(decoder.get());

  const JxlPixelFormat pixelFormat{
      .num_channels = 4,
      .data_type = JXL_TYPE_UINT8,
      .endianness = JXL_NATIVE_ENDIAN,
      .align = 0,
  };
  std::vector<uint8_t> rgba;
  while (true) {
    const JxlDecoderStatus status = JxlDecoderProcessInput(decoder.get());
    if (status == JXL_DEC_BASIC_INFO) {
      JxlBasicInfo info;
      require_condition(
          JxlDecoderGetBasicInfo(decoder.get(), &info) == JXL_DEC_SUCCESS &&
              info.xsize == expectedWidth && info.ysize == expectedHeight,
          "JXL reference dimensions must match the fixture");
      rgba.resize(static_cast<size_t>(expectedWidth) * expectedHeight * 4);
    } else if (status == JXL_DEC_NEED_IMAGE_OUT_BUFFER) {
      require_condition(!rgba.empty(),
                        "JXL reference metadata must precede its pixels");
      require_condition(JxlDecoderSetImageOutBuffer(decoder.get(), &pixelFormat,
                                                    rgba.data(), rgba.size()) ==
                            JXL_DEC_SUCCESS,
                        "JXL reference output buffer must be accepted");
    } else if (status == JXL_DEC_SUCCESS) {
      require_condition(!rgba.empty(),
                        "JXL reference decode must produce pixels");
      return rgba;
    } else if (status != JXL_DEC_FULL_IMAGE) {
      throw std::runtime_error("JXL reference decode failed");
    }
  }
}

std::vector<uint8_t> encode_jxl(uint32_t width, uint32_t height,
                                const std::vector<std::vector<uint8_t>>& frames,
                                const std::vector<uint32_t>& frameDurations,
                                uint32_t loopCount, bool alpha,
                                bool progressive, bool lossless,
                                bool premultipliedAlpha = false) {
  require_condition(!frames.empty(), "JXL fixture must contain a frame");
  require_condition(frameDurations.empty() ||
                        frameDurations.size() == frames.size(),
                    "JXL animation durations must match its frames");
  const size_t expectedFrameBytes = static_cast<size_t>(width) * height * 4;
  for (const auto& frame : frames) {
    require_condition(frame.size() == expectedFrameBytes,
                      "JXL fixture frame dimensions must match metadata");
  }

  auto encoder = JxlEncoderMake(nullptr);
  require_condition(encoder != nullptr, "JXL fixture encoder must be created");

  JxlBasicInfo info;
  JxlEncoderInitBasicInfo(&info);
  info.xsize = width;
  info.ysize = height;
  info.bits_per_sample = 8;
  info.exponent_bits_per_sample = 0;
  info.uses_original_profile = JXL_TRUE;
  info.have_animation = frameDurations.empty() ? JXL_FALSE : JXL_TRUE;
  if (info.have_animation) {
    info.animation.tps_numerator = 1000;
    info.animation.tps_denominator = 1;
    info.animation.num_loops = loopCount;
  }
  if (alpha) {
    info.num_extra_channels = 1;
    info.alpha_bits = 8;
    info.alpha_exponent_bits = 0;
    info.alpha_premultiplied = premultipliedAlpha ? JXL_TRUE : JXL_FALSE;
  }
  require_encoder_success(JxlEncoderSetBasicInfo(encoder.get(), &info),
                          "JXL fixture basic info must be accepted");

  if (alpha) {
    JxlExtraChannelInfo alphaInfo;
    JxlEncoderInitExtraChannelInfo(JXL_CHANNEL_ALPHA, &alphaInfo);
    alphaInfo.bits_per_sample = 8;
    alphaInfo.exponent_bits_per_sample = 0;
    alphaInfo.alpha_premultiplied = premultipliedAlpha ? JXL_TRUE : JXL_FALSE;
    require_encoder_success(
        JxlEncoderSetExtraChannelInfo(encoder.get(), 0, &alphaInfo),
        "JXL fixture alpha info must be accepted");
  }

  JxlColorEncoding colorEncoding;
  JxlColorEncodingSetToSRGB(&colorEncoding, JXL_FALSE);
  require_encoder_success(
      JxlEncoderSetColorEncoding(encoder.get(), &colorEncoding),
      "JXL fixture color encoding must be accepted");

  const JxlPixelFormat pixelFormat{
      .num_channels = alpha ? 4U : 3U,
      .data_type = JXL_TYPE_UINT8,
      .endianness = JXL_NATIVE_ENDIAN,
      .align = 0,
  };
  JxlEncoderFrameSettings* settings =
      JxlEncoderFrameSettingsCreate(encoder.get(), nullptr);
  require_condition(settings != nullptr,
                    "JXL fixture frame settings must be created");
  if (lossless) {
    require_encoder_success(JxlEncoderSetFrameLossless(settings, JXL_TRUE),
                            "JXL fixture lossless mode must be accepted");
  } else {
    require_encoder_success(JxlEncoderSetFrameDistance(settings, 1.0F),
                            "JXL fixture distance must be accepted");
  }
  if (progressive) {
    require_encoder_success(
        JxlEncoderFrameSettingsSetOption(
            settings, JXL_ENC_FRAME_SETTING_PROGRESSIVE_AC, 1),
        "JXL fixture progressive AC mode must be accepted");
    require_encoder_success(
        JxlEncoderFrameSettingsSetOption(
            settings, JXL_ENC_FRAME_SETTING_PROGRESSIVE_DC, 2),
        "JXL fixture progressive DC mode must be accepted");
  }

  for (size_t index = 0; index < frames.size(); ++index) {
    if (info.have_animation) {
      JxlFrameHeader frameHeader;
      JxlEncoderInitFrameHeader(&frameHeader);
      frameHeader.duration = frameDurations[index];
      require_encoder_success(JxlEncoderSetFrameHeader(settings, &frameHeader),
                              "JXL animation frame header must be accepted");
    }
    std::vector<uint8_t> rgb;
    const uint8_t* frameBytes = frames[index].data();
    size_t frameSize = frames[index].size();
    if (!alpha) {
      rgb.reserve(static_cast<size_t>(width) * height * 3);
      for (size_t offset = 0; offset < frames[index].size(); offset += 4) {
        rgb.insert(rgb.end(),
                   frames[index].begin() + static_cast<std::ptrdiff_t>(offset),
                   frames[index].begin() +
                       static_cast<std::ptrdiff_t>(offset + 3));
      }
      frameBytes = rgb.data();
      frameSize = rgb.size();
    }
    require_encoder_success(
        JxlEncoderAddImageFrame(settings, &pixelFormat, frameBytes, frameSize),
        "JXL fixture pixels must be accepted");
  }
  JxlEncoderCloseInput(encoder.get());

  std::vector<uint8_t> encoded(64 * 1024);
  uint8_t* nextOutput = encoded.data();
  size_t availableOutput = encoded.size();
  while (true) {
    const JxlEncoderStatus status =
        JxlEncoderProcessOutput(encoder.get(), &nextOutput, &availableOutput);
    if (status == JXL_ENC_SUCCESS) {
      encoded.resize(static_cast<size_t>(nextOutput - encoded.data()));
      break;
    }
    require_condition(status == JXL_ENC_NEED_MORE_OUTPUT,
                      "JXL fixture must encode successfully");
    const size_t used = static_cast<size_t>(nextOutput - encoded.data());
    encoded.resize(encoded.size() * 2);
    nextOutput = encoded.data() + used;
    availableOutput = encoded.size() - used;
  }
  require_condition(!encoded.empty(), "JXL fixture must produce bytes");
  return encoded;
}

} // namespace

EncodedImageFixture make_jxl_still_fixture(bool progressive) {
  const uint32_t width = progressive ? 768 : kFixtureWidth;
  const uint32_t height = progressive ? 1024 : kFixtureHeight;
  auto expected = progressive ? make_pattern(width, height, 11, false)
                              : make_fixture_pattern(true);
  std::vector<std::vector<uint8_t>> frames;
  frames.push_back(expected);
  auto encoded = encode_jxl(width, height, frames, {}, 0, !progressive,
                            progressive, !progressive);
  expected = decode_jxl_reference(encoded, width, height);
  return EncodedImageFixture{
      .width = width,
      .height = height,
      .expectedRgba = std::move(expected),
      .encoded = std::move(encoded),
  };
}

EncodedImageFixture make_premultiplied_alpha_jxl_fixture() {
  auto straight = make_fixture_pattern(true);
  // A premultiplied JXL stores its colors already multiplied by alpha.
  auto premultiplied = straight;
  for (size_t offset = 0; offset < premultiplied.size(); offset += 4) {
    const uint32_t alpha = premultiplied[offset + 3];
    for (size_t component = 0; component < 3; ++component) {
      premultiplied[offset + component] = static_cast<uint8_t>(
          (premultiplied[offset + component] * alpha + 127) / 255);
    }
  }
  std::vector<std::vector<uint8_t>> frames;
  frames.push_back(std::move(premultiplied));
  auto encoded = encode_jxl(kFixtureWidth, kFixtureHeight, frames, {}, 0, true,
                            false, true, true);
  return EncodedImageFixture{
      .width = kFixtureWidth,
      .height = kFixtureHeight,
      .expectedRgba = std::move(straight),
      .encoded = std::move(encoded),
  };
}

EncodedImageFixture make_large_jxl_benchmark_fixture() {
  constexpr uint32_t width = 1536;
  constexpr uint32_t height = 2048;
  auto expected = make_pattern(width, height, 19, false);
  std::vector<std::vector<uint8_t>> frames;
  frames.push_back(expected);
  auto encoded = encode_jxl(width, height, frames, {}, 0, false, true, false);
  return EncodedImageFixture{
      .width = width,
      .height = height,
      .expectedRgba = {},
      .encoded = std::move(encoded),
  };
}

AnimatedJxlFixture make_animated_jxl_fixture() {
  constexpr uint32_t width = 96;
  constexpr uint32_t height = 144;
  constexpr int32_t loopCount = 2;
  const std::vector<uint64_t> durationsMillis{80, 125, 240};
  std::vector<std::vector<uint8_t>> frames;
  frames.push_back(make_pattern(width, height, 3, false));
  frames.push_back(make_pattern(width, height, 71, false));
  frames.push_back(make_pattern(width, height, 149, false));
  std::vector<uint32_t> durations;
  durations.reserve(durationsMillis.size());
  for (const uint64_t duration : durationsMillis) {
    durations.push_back(static_cast<uint32_t>(duration));
  }
  auto encoded = encode_jxl(width, height, frames, durations, loopCount, false,
                            false, true);
  return AnimatedJxlFixture{
      .width = width,
      .height = height,
      .loopCount = loopCount,
      .durationsMillis = durationsMillis,
      .expectedFrames = std::move(frames),
      .encoded = std::move(encoded),
  };
}
