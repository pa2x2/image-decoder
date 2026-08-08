#include "fixtures/gif_fixture.h"

#include "support/decode_harness.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace {

constexpr uint32_t kGifFixtureWidth = 4;
constexpr uint32_t kGifFixtureHeight = 3;

void append_little_endian_u16(std::vector<uint8_t>& output, uint16_t value) {
  output.push_back(static_cast<uint8_t>(value));
  output.push_back(static_cast<uint8_t>(value >> 8));
}

void append_lzw_indices(std::vector<uint8_t>& output,
                        const std::vector<uint8_t>& indices) {
  constexpr uint8_t kMinimumCodeSize = 2;
  constexpr uint8_t kCodeSize = kMinimumCodeSize + 1;
  constexpr uint8_t kClearCode = 1U << kMinimumCodeSize;
  constexpr uint8_t kEndCode = kClearCode + 1;

  std::vector<uint8_t> encoded;
  uint32_t bits = 0;
  uint8_t bitCount = 0;
  const auto appendCode = [&](uint8_t code) mutable {
    bits |= static_cast<uint32_t>(code) << bitCount;
    bitCount += kCodeSize;
    while (bitCount >= 8) {
      encoded.push_back(static_cast<uint8_t>(bits));
      bits >>= 8;
      bitCount -= 8;
    }
  };
  for (const uint8_t index : indices) {
    appendCode(kClearCode);
    appendCode(index);
  }
  appendCode(kEndCode);
  if (bitCount > 0) {
    encoded.push_back(static_cast<uint8_t>(bits));
  }

  output.push_back(kMinimumCodeSize);
  for (size_t offset = 0; offset < encoded.size();) {
    const size_t count = std::min<size_t>(255, encoded.size() - offset);
    output.push_back(static_cast<uint8_t>(count));
    output.insert(output.end(), encoded.begin() + static_cast<std::ptrdiff_t>(offset),
                  encoded.begin() + static_cast<std::ptrdiff_t>(offset + count));
    offset += count;
  }
  output.push_back(0);
}

void append_graphic_control(std::vector<uint8_t>& output, uint16_t duration,
                            uint8_t disposal, bool transparent) {
  output.insert(output.end(), {0x21, 0xF9, 0x04,
                               static_cast<uint8_t>((disposal << 2) |
                                                    (transparent ? 1 : 0))});
  append_little_endian_u16(output, duration);
  output.push_back(0);
  output.push_back(0);
}

void append_image(std::vector<uint8_t>& output, uint16_t left, uint16_t top,
                  uint16_t width, uint16_t height,
                  const std::vector<uint8_t>& indices) {
  require_condition(indices.size() == static_cast<size_t>(width) * height,
                    "GIF fixture image must contain every source pixel");
  output.push_back(0x2C);
  append_little_endian_u16(output, left);
  append_little_endian_u16(output, top);
  append_little_endian_u16(output, width);
  append_little_endian_u16(output, height);
  output.push_back(0);
  append_lzw_indices(output, indices);
}

std::vector<uint8_t> make_canvas(uint8_t color) {
  std::vector<uint8_t> output(static_cast<size_t>(kGifFixtureWidth) *
                              kGifFixtureHeight * 4);
  const std::array<std::array<uint8_t, 4>, 4> palette = {
      std::array<uint8_t, 4>{0, 0, 0, 255},
      std::array<uint8_t, 4>{255, 0, 0, 255},
      std::array<uint8_t, 4>{0, 255, 0, 255},
      std::array<uint8_t, 4>{0, 0, 255, 255},
  };
  for (size_t offset = 0; offset < output.size(); offset += 4) {
    std::copy(palette[color].begin(), palette[color].end(),
              output.begin() + static_cast<std::ptrdiff_t>(offset));
  }
  return output;
}

void set_pixel(std::vector<uint8_t>& canvas, uint32_t x, uint32_t y,
               uint8_t color) {
  const std::array<std::array<uint8_t, 4>, 4> palette = {
      std::array<uint8_t, 4>{0, 0, 0, 255},
      std::array<uint8_t, 4>{255, 0, 0, 255},
      std::array<uint8_t, 4>{0, 255, 0, 255},
      std::array<uint8_t, 4>{0, 0, 255, 255},
  };
  const size_t offset = (static_cast<size_t>(y) * kGifFixtureWidth + x) * 4;
  std::copy(palette[color].begin(), palette[color].end(),
            canvas.begin() + static_cast<std::ptrdiff_t>(offset));
}

} // namespace

AnimatedGifFixture make_animated_gif_fixture() {
  std::vector<uint8_t> encoded = {
      'G', 'I', 'F', '8', '9', 'a',
      static_cast<uint8_t>(kGifFixtureWidth), 0,
      static_cast<uint8_t>(kGifFixtureHeight), 0,
      0xF1, 0, 0,
      0, 0, 0,
      255, 0, 0,
      0, 255, 0,
      0, 0, 255,
      0x21, 0xFF, 0x0B,
      'N', 'E', 'T', 'S', 'C', 'A', 'P', 'E', '2', '.', '0',
      0x03, 0x01, 0x02, 0x00, 0x00,
  };
  append_graphic_control(encoded, 7, 0, false);
  append_image(encoded, 0, 0, kGifFixtureWidth, kGifFixtureHeight,
               std::vector<uint8_t>(kGifFixtureWidth * kGifFixtureHeight, 1));
  append_graphic_control(encoded, 12, 2, true);
  append_image(encoded, 1, 1, 2, 1, {2, 0});
  append_graphic_control(encoded, 5, 3, false);
  append_image(encoded, 2, 0, 1, 2, {3, 3});
  append_graphic_control(encoded, 9, 0, false);
  append_image(encoded, 0, 2, 1, 1, {2});
  encoded.insert(encoded.end(), {0x21, 0xFE, 0x04, 't', 'a', 'i', 'l', 0x00,
                                 0x3B});

  auto first = make_canvas(1);
  auto second = first;
  set_pixel(second, 1, 1, 2);
  auto third = first;
  set_pixel(third, 1, 1, 0);
  set_pixel(third, 2, 1, 3);
  set_pixel(third, 2, 0, 3);
  auto fourth = first;
  set_pixel(fourth, 1, 1, 0);
  set_pixel(fourth, 2, 1, 0);
  set_pixel(fourth, 0, 2, 2);

  return AnimatedGifFixture{
      .width = kGifFixtureWidth,
      .height = kGifFixtureHeight,
      .loopCount = 3,
      .durationsMillis = {70, 120, 50, 90},
      .expectedFrames = {std::move(first), std::move(second), std::move(third),
                         std::move(fourth)},
      .encoded = std::move(encoded),
  };
}
