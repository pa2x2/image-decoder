#include "fixtures/jpeg_fixture.h"

#include "fixtures/image_pattern.h"
#include "support/decode_harness.h"

#include <jpeglib.h>

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <utility>
#include <vector>

namespace {

std::vector<uint8_t> encode_jpeg(const std::vector<uint8_t>& rgba,
                                 bool progressive) {
  jpeg_compress_struct compressor{};
  jpeg_error_mgr errorManager{};
  compressor.err = jpeg_std_error(&errorManager);
  jpeg_create_compress(&compressor);

  unsigned char* encodedBytes = nullptr;
  unsigned long encodedSize = 0;
  jpeg_mem_dest(&compressor, &encodedBytes, &encodedSize);
  compressor.image_width = kFixtureWidth;
  compressor.image_height = kFixtureHeight;
  compressor.input_components = 3;
  compressor.in_color_space = JCS_RGB;
  jpeg_set_defaults(&compressor);
  jpeg_set_quality(&compressor, 92, TRUE);
  if (progressive) {
    jpeg_simple_progression(&compressor);
  }
  jpeg_start_compress(&compressor, TRUE);

  std::vector<uint8_t> rgb(static_cast<size_t>(kFixtureWidth) * 3);
  while (compressor.next_scanline < compressor.image_height) {
    const size_t sourceOffset =
        static_cast<size_t>(compressor.next_scanline) * kFixtureWidth * 4;
    for (uint32_t x = 0; x < kFixtureWidth; ++x) {
      std::memcpy(rgb.data() + static_cast<size_t>(x) * 3,
                  rgba.data() + sourceOffset + static_cast<size_t>(x) * 4, 3);
    }
    JSAMPROW row = rgb.data();
    jpeg_write_scanlines(&compressor, &row, 1);
  }
  jpeg_finish_compress(&compressor);

  std::vector<uint8_t> output(encodedBytes, encodedBytes + encodedSize);
  std::free(encodedBytes);
  jpeg_destroy_compress(&compressor);
  return output;
}

std::vector<uint8_t>
decode_jpeg_reference(const std::vector<uint8_t>& encoded) {
  jpeg_decompress_struct decompressor{};
  jpeg_error_mgr errorManager{};
  decompressor.err = jpeg_std_error(&errorManager);
  jpeg_create_decompress(&decompressor);
  jpeg_mem_src(&decompressor, encoded.data(), encoded.size());
  require_condition(jpeg_read_header(&decompressor, TRUE) == JPEG_HEADER_OK,
                    "JPEG fixture must have a valid header");
  decompressor.out_color_space = JCS_EXT_RGBA;
  require_condition(jpeg_start_decompress(&decompressor) == TRUE,
                    "JPEG fixture must start decoding");

  std::vector<uint8_t> output(static_cast<size_t>(decompressor.output_width) *
                              decompressor.output_height * 4);
  while (decompressor.output_scanline < decompressor.output_height) {
    JSAMPROW row =
        output.data() + static_cast<size_t>(decompressor.output_scanline) *
                            decompressor.output_width * 4;
    require_condition(jpeg_read_scanlines(&decompressor, &row, 1) == 1,
                      "JPEG fixture must decode every row");
  }
  require_condition(jpeg_finish_decompress(&decompressor) == TRUE,
                    "JPEG fixture must finish decoding");
  jpeg_destroy_decompress(&decompressor);
  return output;
}

} // namespace

EncodedImageFixture make_jpeg_fixture(bool progressive) {
  auto source = make_fixture_pattern(false);
  auto encoded = encode_jpeg(source, progressive);
  return EncodedImageFixture{
      .width = kFixtureWidth,
      .height = kFixtureHeight,
      .expectedRgba = decode_jpeg_reference(encoded),
      .encoded = std::move(encoded),
  };
}
