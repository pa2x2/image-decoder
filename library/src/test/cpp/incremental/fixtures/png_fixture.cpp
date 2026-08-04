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

std::vector<uint8_t> encode_png(const std::vector<uint8_t>& rgba, bool adam7) {
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
  png_set_IHDR(writer, info, kFixtureWidth, kFixtureHeight, 8,
               PNG_COLOR_TYPE_RGBA,
               adam7 ? PNG_INTERLACE_ADAM7 : PNG_INTERLACE_NONE,
               PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
  png_write_info(writer, info);

  const int passes = adam7 ? png_set_interlace_handling(writer) : 1;
  for (int pass = 0; pass < passes; ++pass) {
    for (uint32_t y = 0; y < kFixtureHeight; ++y) {
      png_write_row(writer,
                    const_cast<png_bytep>(rgba.data() + static_cast<size_t>(y) *
                                                            kFixtureWidth * 4));
    }
  }
  png_write_end(writer, info);
  png_destroy_write_struct(&writer, &info);
  return output;
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

std::vector<uint8_t> make_apng_fallback_fixture() {
  auto png = make_png_fixture(false).encoded;
  constexpr size_t kAfterIhdr = 8 + 4 + 4 + 13 + 4;
  require_condition(png.size() > kAfterIhdr,
                    "PNG fixture must contain a complete IHDR chunk");

  std::vector<uint8_t> animationControl;
  write_big_endian_u32(animationControl, 8);
  const std::array<uint8_t, 4> type = {'a', 'c', 'T', 'L'};
  animationControl.insert(animationControl.end(), type.begin(), type.end());
  write_big_endian_u32(animationControl, 1);
  write_big_endian_u32(animationControl, 0);
  uLong checksum = crc32(0, Z_NULL, 0);
  checksum = crc32(checksum, type.data(), type.size());
  checksum = crc32(checksum, animationControl.data() + 8, 8);
  write_big_endian_u32(animationControl, static_cast<uint32_t>(checksum));

  png.insert(png.begin() + kAfterIhdr, animationControl.begin(),
             animationControl.end());
  return png;
}
