#ifndef IMAGEDECODER_ALPHA_PREMULTIPLICATION_H
#define IMAGEDECODER_ALPHA_PREMULTIPLICATION_H

#include <cstddef>
#include <cstdint>

// Android bitmaps hold premultiplied alpha, while the decoders, color
// transforms and resamplers of this library work on straight alpha. These
// convert interleaved 8-bit RGBA pixels between the two.

// Writes straight pixels premultiplied by their alpha, rounded to nearest.
// `straight` and `premultiplied` may be the same buffer.
void premultiply_rgba(const uint8_t* straight, uint8_t* premultiplied,
                      size_t pixelCount);

// Divides premultiplied pixels by their alpha in place, rounded to nearest.
// Fully transparent pixels have no color to recover and become 0.
void unpremultiply_rgba(uint8_t* pixels, size_t pixelCount);

#endif // IMAGEDECODER_ALPHA_PREMULTIPLICATION_H
