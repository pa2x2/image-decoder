//
// Created by len on 24/12/20.
//

#include "decoder_png.h"
#include "downsampling/adam7_box_downsampler.h"
#include "downsampling/box_downsampler.h"
#include <algorithm>
#include <cstring>

static void png_skip_rows(png_structrp png_ptr, png_uint_32 num_rows) {
  for (png_uint_32 i = 0; i < num_rows; ++i) {
    png_read_row(png_ptr, nullptr, nullptr);
  }
}

PngDecoder::PngDecoder(std::shared_ptr<Stream>&& stream, bool cropBorders,
                       cmsHPROFILE targetProfile)
    : BaseDecoder(std::move(stream), cropBorders, targetProfile) {
  this->info = parseInfo();
}

PngDecodeSession::PngDecodeSession(Stream* stream)
    : png(nullptr), pinfo(nullptr),
      reader({.bytes = stream->bytes, .read = 0, .remain = stream->size}) {}

#pragma clang diagnostic push
#pragma ide diagnostic ignored "EndlessLoop"
void PngDecodeSession::init() {
  auto errorFn = [](png_struct*, png_const_charp msg) {
    throw std::runtime_error(msg);
  };
  auto warnFn = [](png_struct*, png_const_charp msg) { LOGW("%s", msg); };
  png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, errorFn, warnFn);
  if (!png) {
    throw std::runtime_error("Failed to create png read struct");
  }
  pinfo = png_create_info_struct(png);
  if (!pinfo) {
    throw std::runtime_error("Failed to create png info struct");
  }

  auto readFn = [](png_struct* p, png_byte* data, png_size_t length) {
    auto* r = (PngReader*)png_get_io_ptr(p);
    uint32_t next = std::min(r->remain, (uint32_t)length);
    if (next > 0) {
      memcpy(data, r->bytes + r->read, next);
      r->read += next;
      r->remain -= next;
    }
  };
  png_set_read_fn(png, &reader, readFn);
  png_read_info(png, pinfo);
}
#pragma clang diagnostic pop

PngDecodeSession::~PngDecodeSession() {
  png_destroy_read_struct(&png, &pinfo, nullptr);
}

std::unique_ptr<PngDecodeSession> PngDecoder::initDecodeSession() {
  auto session = std::make_unique<PngDecodeSession>(stream.get());
  session->init();
  return session;
}

ImageInfo PngDecoder::parseInfo() {
  auto session = initDecodeSession();
  auto png = session->png;
  auto pinfo = session->pinfo;

  uint32_t imageWidth = png_get_image_width(png, pinfo);
  uint32_t imageHeight = png_get_image_height(png, pinfo);

  Rect bounds = {.x = 0, .y = 0, .width = imageWidth, .height = imageHeight};
  if (cropBorders) {
    try {
      auto pixels = std::make_unique<uint8_t[]>(imageWidth * imageHeight);

      uint8_t colorType = png_get_color_type(png, pinfo);
      uint8_t bitDepth = png_get_bit_depth(png, pinfo);

      png_set_expand(png);
      if (bitDepth == 16) {
        png_set_scale_16(png);
      }
      if (colorType & (uint8_t)PNG_COLOR_MASK_COLOR) {
        png_set_rgb_to_gray(png, 1, -1, -1);
        png_set_strip_alpha(png);
      } else if (colorType & (uint8_t)PNG_COLOR_MASK_ALPHA) {
        png_set_strip_alpha(png);
      }

      int32_t passes = png_set_interlace_handling(png);

      uint8_t* pixelsPos;
      while (--passes >= 0) {
        pixelsPos = pixels.get();
        for (uint32_t i = 0; i < imageHeight; ++i) {
          png_read_row(png, pixelsPos, nullptr);
          pixelsPos += imageWidth;
        }
      }
      bounds = findBorders(pixels.get(), imageWidth, imageHeight);
    } catch (std::bad_alloc& ex) {
      LOGW("Couldn't crop borders on a PNG image of size %dx%d", imageWidth,
           imageHeight);
    }
  }

  return ImageInfo{
      .imageWidth = imageWidth,
      .imageHeight = imageHeight,
      .isAnimated = false,
      .bounds = bounds,
  };
}

cmsHPROFILE PngDecoder::getColorProfile(png_struct* png, png_info* pinfo,
                                        uint8_t colorType) {
  if (!png_get_valid(png, pinfo, PNG_INFO_iCCP)) {
    return nullptr;
  }

  png_charp name;
  png_bytep icc_data;
  png_uint_32 icc_size;
  int comp_type;
  png_get_iCCP(png, pinfo, &name, &comp_type, &icc_data, &icc_size);

  cmsHPROFILE src_profile = cmsOpenProfileFromMem(icc_data, icc_size);
  cmsColorSpaceSignature profileSpace = cmsGetColorSpace(src_profile);

  bool rgb = colorType & PNG_COLOR_MASK_COLOR;

  if (rgb && profileSpace != cmsSigRgbData ||
      !rgb && profileSpace != cmsSigGrayData) {
    cmsCloseProfile(src_profile);
    return nullptr;
  }

  return src_profile;
}

void PngDecoder::decode(uint8_t* outPixels, Rect outRect, Rect inRect,
                        uint32_t sampleSize) {
  auto session = initDecodeSession();
  auto png = session->png;
  auto pinfo = session->pinfo;

  uint8_t colorType = png_get_color_type(png, pinfo);
  uint8_t bitDepth = png_get_bit_depth(png, pinfo);

  png_set_expand(png);

  if (bitDepth == 16) {
    png_set_scale_16(png);
  }

  cmsHPROFILE src_profile = getColorProfile(png, pinfo, colorType);
  if (!src_profile) {
    src_profile = cmsCreate_sRGBProfile();
    inType = TYPE_RGBA_8;

    if (colorType == PNG_COLOR_TYPE_GRAY ||
        colorType == PNG_COLOR_TYPE_GRAY_ALPHA) {
      png_set_gray_to_rgb(png);
    }
  } else {
    if (colorType == PNG_COLOR_TYPE_GRAY_ALPHA ||
        colorType == PNG_COLOR_TYPE_GRAY) {
      inType = TYPE_GRAYA_8;
    } else {
      inType = TYPE_RGBA_8;
    }
  }

  if (!(colorType & PNG_COLOR_MASK_ALPHA)) {
    png_set_add_alpha(png, 0xff, PNG_FILLER_AFTER);
  }

  cmsColorSpaceSignature profileSpace = cmsGetColorSpace(src_profile);

  useTransform = true;

  transform = cmsCreateTransform(
      src_profile, inType, targetProfile, TYPE_RGBA_8,
      cmsGetHeaderRenderingIntent(src_profile), cmsFLAGS_COPY_ALPHA);

  cmsCloseProfile(src_profile);

  const bool interlaced =
      png_get_interlace_type(png, pinfo) != PNG_INTERLACE_NONE;
  // Sampled decodes of interlaced images read the reduced rows of each pass
  // themselves, so libpng only merges passes into full rows when unsampled.
  const int32_t passes = sampleSize == 1 ? png_set_interlace_handling(png) : 1;

  png_read_update_info(png, pinfo);

  uint32_t inComponents = png_get_channels(png, pinfo);
  size_t inStride = static_cast<size_t>(info.imageWidth) * inComponents;
  size_t inStrideOffset = static_cast<size_t>(inRect.x) * inComponents;

  uint8_t* outPixelsPos = outPixels;
  size_t outStride = static_cast<size_t>(outRect.width) * inComponents;

  if (sampleSize == 1) {
    uint32_t inRemainY = info.imageHeight - inRect.height - inRect.y;

    if (passes == 1) {
      auto inRow = std::vector<uint8_t>(inStride);
      auto* inRowPtr = inRow.data();
      uint8_t* rowToWrite = inRowPtr + inStrideOffset;

      png_skip_rows(png, inRect.y);
      for (uint32_t i = 0; i < inRect.height; ++i) {
        png_read_row(png, inRowPtr, nullptr);
        memcpy(outPixelsPos, rowToWrite, outStride);
        outPixelsPos += outStride;
      }
      png_skip_rows(png, inRemainY);
    } else {
      // decode the image in full
      auto inPixels = std::vector<uint8_t>(inStride * inRect.height);
      auto* inPixelsPos = inPixels.data();

      for (int32_t pass = 0; pass < passes; ++pass) {
        png_skip_rows(png, inRect.y);
        for (uint32_t i = 0; i < inRect.height; ++i) {
          png_read_row(png, inPixelsPos, nullptr);
          inPixelsPos += inStride;
        }
        png_skip_rows(png, inRemainY);
        inPixelsPos = inPixels.data();
      }

      for (uint32_t i = 0; i < inRect.height; ++i) {
        memcpy(outPixelsPos, inPixelsPos + inStrideOffset, outStride);
        inPixelsPos += inStride;
        outPixelsPos += outStride;
      }
    }
  } else if (!interlaced) {
    // Alpha is always present: missing alpha is filled in above.
    BoxDownsampler downsampler(outRect.width, sampleSize, inComponents, true);
    auto inRow = std::vector<uint8_t>(inStride);
    uint8_t* inRowPtr = inRow.data();

    png_skip_rows(png, inRect.y);
    for (uint32_t i = 0; i < outRect.height; ++i) {
      for (uint32_t row = 0; row < sampleSize; ++row) {
        png_read_row(png, inRowPtr, nullptr);
        downsampler.addRow(inRowPtr + inStrideOffset);
      }
      downsampler.writeRow(outPixelsPos);
      outPixelsPos += outStride;
    }
  } else {
    // Each pixel belongs to exactly one Adam7 pass, and the passes each span
    // the whole image, so blocks complete only with the last pass.
    Adam7BoxDownsampler downsampler(inRect, outRect, sampleSize, inComponents,
                                    true);
    // libpng copies a full image row even for the narrower pass rows.
    auto passRow = std::vector<uint8_t>(png_get_rowbytes(png, pinfo));

    for (int pass = 0; pass < Adam7BoxDownsampler::kPasses; ++pass) {
      // libpng skips the passes that have no pixels.
      if (PNG_PASS_COLS(info.imageWidth, pass) == 0) {
        continue;
      }
      const uint32_t passRows = PNG_PASS_ROWS(info.imageHeight, pass);
      for (uint32_t row = 0; row < passRows; ++row) {
        if (downsampler.coversPassRow(pass, row)) {
          png_read_row(png, passRow.data(), nullptr);
          downsampler.addPassRow(pass, row, passRow.data());
        } else {
          png_read_row(png, nullptr, nullptr);
        }
      }
    }
    downsampler.write(outPixels);
  }
}
