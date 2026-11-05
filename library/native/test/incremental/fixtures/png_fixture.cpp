#include "fixtures/png_fixture.h"

#include "fixtures/image_pattern.h"
#include "support/decode_harness.h"

#include <png.h>
#include <zlib.h>

#include <array>
#include <cstddef>
#include <utility>
#include <vector>

namespace {

void write_big_endian_u32(std::vector<uint8_t>& output, uint32_t value) {
  output.push_back(static_cast<uint8_t>(value >> 24));
  output.push_back(static_cast<uint8_t>(value >> 16));
  output.push_back(static_cast<uint8_t>(value >> 8));
  output.push_back(static_cast<uint8_t>(value));
}

void write_big_endian_u16(std::vector<uint8_t>& output, uint16_t value) {
  output.push_back(static_cast<uint8_t>(value >> 8));
  output.push_back(static_cast<uint8_t>(value));
}

uint32_t read_big_endian_u32(const uint8_t* bytes) {
  return static_cast<uint32_t>(bytes[0]) << 24 |
         static_cast<uint32_t>(bytes[1]) << 16 |
         static_cast<uint32_t>(bytes[2]) << 8 | static_cast<uint32_t>(bytes[3]);
}

void append_chunk(std::vector<uint8_t>& output,
                  const std::array<uint8_t, 4>& type,
                  const std::vector<uint8_t>& data) {
  write_big_endian_u32(output, static_cast<uint32_t>(data.size()));
  output.insert(output.end(), type.begin(), type.end());
  output.insert(output.end(), data.begin(), data.end());
  uLong checksum = crc32(0, Z_NULL, 0);
  checksum = crc32(checksum, type.data(), type.size());
  if (!data.empty()) {
    checksum = crc32(checksum, data.data(), data.size());
  }
  write_big_endian_u32(output, static_cast<uint32_t>(checksum));
}

std::vector<std::vector<uint8_t>>
chunk_payloads(const std::vector<uint8_t>& png,
               const std::array<uint8_t, 4>& wantedType) {
  std::vector<std::vector<uint8_t>> payloads;
  size_t offset = 8;
  while (offset + 12 <= png.size()) {
    const uint32_t size = read_big_endian_u32(png.data() + offset);
    require_condition(offset + static_cast<size_t>(size) + 12 <= png.size(),
                      "PNG fixture chunks must be complete");
    if (std::equal(wantedType.begin(), wantedType.end(),
                   png.begin() + static_cast<std::ptrdiff_t>(offset + 4))) {
      payloads.emplace_back(
          png.begin() + static_cast<std::ptrdiff_t>(offset + 8),
          png.begin() + static_cast<std::ptrdiff_t>(offset + 8 + size));
    }
    offset += static_cast<size_t>(size) + 12;
  }
  return payloads;
}

std::vector<uint8_t> make_frame_control(uint32_t sequence,
                                        uint16_t durationMillis) {
  std::vector<uint8_t> control;
  write_big_endian_u32(control, sequence);
  write_big_endian_u32(control, kFixtureWidth);
  write_big_endian_u32(control, kFixtureHeight);
  write_big_endian_u32(control, 0);
  write_big_endian_u32(control, 0);
  write_big_endian_u16(control, durationMillis);
  write_big_endian_u16(control, 1000);
  control.push_back(0);
  control.push_back(0);
  return control;
}

// `pixels` holds one byte per sample; samples narrower than a byte are packed
// by the writer.
std::vector<uint8_t> encode_png(const std::vector<uint8_t>& pixels,
                                int colorType, int bitDepth, bool adam7) {
  std::vector<uint8_t> output;
  png_structp writer =
      png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  require_condition(writer != nullptr, "PNG fixture writer must be created");
  png_infop info = png_create_info_struct(writer);
  require_condition(info != nullptr, "PNG fixture info must be created");

  png_set_write_fn(
      writer, &output,
      [](png_structp png, png_bytep bytes, png_size_t size) {
        auto* destination =
            static_cast<std::vector<uint8_t>*>(png_get_io_ptr(png));
        destination->insert(destination->end(), bytes, bytes + size);
      },
      [](png_structp) {});
  png_set_IHDR(writer, info, kFixtureWidth, kFixtureHeight, bitDepth, colorType,
               adam7 ? PNG_INTERLACE_ADAM7 : PNG_INTERLACE_NONE,
               PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
  png_write_info(writer, info);
  if (bitDepth < 8) {
    png_set_packing(writer);
  }

  const size_t rowBytes = pixels.size() / kFixtureHeight;
  const int passes = adam7 ? png_set_interlace_handling(writer) : 1;
  for (int pass = 0; pass < passes; ++pass) {
    for (uint32_t y = 0; y < kFixtureHeight; ++y) {
      png_write_row(writer,
                    const_cast<png_bytep>(pixels.data() +
                                          static_cast<size_t>(y) * rowBytes));
    }
  }
  png_write_end(writer, info);
  png_destroy_write_struct(&writer, &info);
  return output;
}

std::vector<uint8_t> encode_png(const std::vector<uint8_t>& rgba, bool adam7) {
  return encode_png(rgba, PNG_COLOR_TYPE_RGBA, 8, adam7);
}

} // namespace

EncodedImageFixture make_png_fixture(bool adam7) {
  auto source = make_fixture_pattern(true);
  return EncodedImageFixture{
      .width = kFixtureWidth,
      .height = kFixtureHeight,
      .expectedRgba = source,
      .encoded = encode_png(source, adam7),
  };
}

EncodedImageFixture make_packed_gray_png_fixture(bool adam7) {
  constexpr int bitDepth = 2;
  std::vector<uint8_t> levels(static_cast<size_t>(kFixtureWidth) *
                              kFixtureHeight);
  std::vector<uint8_t> expectedRgba(levels.size() * 4);
  for (uint32_t y = 0; y < kFixtureHeight; ++y) {
    for (uint32_t x = 0; x < kFixtureWidth; ++x) {
      const size_t pixel = static_cast<size_t>(y) * kFixtureWidth + x;
      levels[pixel] = static_cast<uint8_t>((x * 3 + y * 5 + (x ^ y)) & 3);
      const auto gray = static_cast<uint8_t>(levels[pixel] * 85);
      expectedRgba[pixel * 4] = gray;
      expectedRgba[pixel * 4 + 1] = gray;
      expectedRgba[pixel * 4 + 2] = gray;
      expectedRgba[pixel * 4 + 3] = 255;
    }
  }
  return EncodedImageFixture{
      .width = kFixtureWidth,
      .height = kFixtureHeight,
      .expectedRgba = std::move(expectedRgba),
      .encoded = encode_png(levels, PNG_COLOR_TYPE_GRAY, bitDepth, adam7),
  };
}

AnimatedPngFixture make_animated_png_fixture() {
  constexpr int32_t loopCount = 3;
  std::vector<std::vector<uint8_t>> frames;
  frames.push_back(make_fixture_pattern(true));
  auto secondFrame = frames.front();
  for (size_t offset = 0; offset < secondFrame.size(); offset += 4) {
    std::swap(secondFrame[offset], secondFrame[offset + 2]);
  }
  frames.push_back(std::move(secondFrame));
  const auto firstPng = encode_png(frames[0], false);
  const auto secondPng = encode_png(frames[1], false);
  const std::array<uint8_t, 4> ihdrType = {'I', 'H', 'D', 'R'};
  const std::array<uint8_t, 4> idatType = {'I', 'D', 'A', 'T'};
  const auto ihdr = chunk_payloads(firstPng, ihdrType);
  const auto firstData = chunk_payloads(firstPng, idatType);
  const auto secondData = chunk_payloads(secondPng, idatType);
  require_condition(ihdr.size() == 1 && !firstData.empty() &&
                        !secondData.empty(),
                    "APNG fixture source chunks must be available");

  std::vector<uint8_t> encoded(firstPng.begin(), firstPng.begin() + 8);
  append_chunk(encoded, ihdrType, ihdr.front());
  std::vector<uint8_t> animationControl;
  write_big_endian_u32(animationControl, 2);
  write_big_endian_u32(animationControl, loopCount);
  append_chunk(encoded, {'a', 'c', 'T', 'L'}, animationControl);
  append_chunk(encoded, {'f', 'c', 'T', 'L'}, make_frame_control(0, 100));
  for (const auto& data : firstData) {
    append_chunk(encoded, idatType, data);
  }
  append_chunk(encoded, {'f', 'c', 'T', 'L'}, make_frame_control(1, 240));
  uint32_t sequence = 2;
  for (const auto& data : secondData) {
    std::vector<uint8_t> frameData;
    write_big_endian_u32(frameData, sequence++);
    frameData.insert(frameData.end(), data.begin(), data.end());
    append_chunk(encoded, {'f', 'd', 'A', 'T'}, frameData);
  }
  append_chunk(encoded, {'I', 'E', 'N', 'D'}, {});
  return AnimatedPngFixture{
      .width = kFixtureWidth,
      .height = kFixtureHeight,
      .loopCount = loopCount,
      .durationsMillis = {100, 240},
      .expectedFrames = std::move(frames),
      .encoded = std::move(encoded),
  };
}
