//
// Created by len on 23/12/20.
//

#include "decoder_jpeg.h"
#include "cmyk.h"
#include "log.h"
#include "box_downsampler.h"
#include <algorithm>
#include <cstring>

JpegDecoder::JpegDecoder(std::shared_ptr<Stream>&& stream, bool cropBorders,
                         cmsHPROFILE targetProfile)
    : BaseDecoder(std::move(stream), cropBorders, targetProfile) {
  this->info = parseInfo();
}

JpegDecodeSession::JpegDecodeSession()
    : jinfo(jpeg_decompress_struct{}), jerr(jpeg_error_mgr{}) {
  jinfo.err = jpeg_std_error(&jerr);
  jerr.error_exit = [](j_common_ptr info) {
    char jpegLastErrorMsg[JMSG_LENGTH_MAX];
    (*(info->err->format_message))(info, jpegLastErrorMsg);
    throw std::runtime_error(jpegLastErrorMsg);
  };
}

void JpegDecodeSession::init(Stream* stream) {
  jpeg_create_decompress(&jinfo);
  jpeg_mem_src(&jinfo, stream->bytes, stream->size);
  jpeg_save_markers(&jinfo, JPEG_APP0 + 2, 0xFFFF);
  jpeg_read_header(&jinfo, true);
}

JpegDecodeSession::~JpegDecodeSession() { jpeg_destroy_decompress(&jinfo); }

std::unique_ptr<JpegDecodeSession> JpegDecoder::initDecodeSession() {
  auto session = std::make_unique<JpegDecodeSession>();
  session->init(stream.get());
  return session;
}

ImageInfo JpegDecoder::parseInfo() {
  auto session = initDecodeSession();
  auto jinfo = session->jinfo;

  uint32_t imageWidth = jinfo.image_width;
  uint32_t imageHeight = jinfo.image_height;

  Rect bounds = {.x = 0, .y = 0, .width = imageWidth, .height = imageHeight};
  if (cropBorders) {
    try {
      auto pixels = std::vector<uint8_t>(imageWidth * imageHeight);

      jinfo.out_color_space = JCS_GRAYSCALE;
      jpeg_start_decompress(&jinfo);

      uint8_t* pixelsPtr = pixels.data();
      while (jinfo.output_scanline < jinfo.output_height) {
        uint8_t* offset = pixelsPtr + jinfo.output_scanline * imageWidth;
        jpeg_read_scanlines(&jinfo, &offset, 1);
      }
      jpeg_finish_decompress(&jinfo);
      bounds = findBorders(pixels.data(), imageWidth, imageHeight);
    } catch (std::exception& ex) {
      LOGW("Couldn't crop borders on a JPEG image of size %dx%d", imageWidth,
           imageHeight);
    }
  }

  return ImageInfo{
      .imageWidth = jinfo.image_width,
      .imageHeight = jinfo.image_height,
      .isAnimated = false,
      .bounds = bounds,
  };
}

cmsHPROFILE JpegDecoder::getColorProfile(jpeg_decompress_struct* jinfo) {
  JOCTET* icc_data;
  unsigned int icc_size;
  if (!jpeg_read_icc_profile(jinfo, &icc_data, &icc_size)) {
    return nullptr;
  }
  cmsHPROFILE src_profile = cmsOpenProfileFromMem(icc_data, icc_size);
  free(icc_data);

  cmsColorSpaceSignature profileSpace = cmsGetColorSpace(src_profile);

  auto colorspace = jinfo->jpeg_color_space;

  if ((colorspace == JCS_GRAYSCALE && profileSpace != cmsSigGrayData) ||
      (colorspace == JCS_CMYK && profileSpace != cmsSigCmykData) ||
      (colorspace == JCS_YCCK && profileSpace != cmsSigCmykData) ||
      (colorspace == JCS_RGB && profileSpace != cmsSigRgbData) ||
      (colorspace == JCS_YCbCr && profileSpace != cmsSigRgbData)) {
    cmsCloseProfile(src_profile);
    return nullptr;
  }

  return src_profile;
}

void JpegDecoder::decode(uint8_t* outPixels, Rect outRect, Rect inRect,
                         uint32_t sampleSize) {
  auto session = initDecodeSession();
  auto* jinfo = &session->jinfo;

  if (jinfo->jpeg_color_space == JCS_CMYK ||
      jinfo->jpeg_color_space == JCS_YCCK) {
    // libjpeg can convert YCCK to CMYK
    inType = TYPE_CMYK_8;
  } else if (jinfo->jpeg_color_space == JCS_GRAYSCALE) {
    inType = TYPE_GRAY_8;
  } else {
    inType = TYPE_RGBA_8;
  }

  cmsHPROFILE src_profile = getColorProfile(jinfo);
  if (!src_profile) {
    if (inType == TYPE_CMYK_8) {
      src_profile = cmsOpenProfileFromMem(CMYK_USWebCoatedSWOP_icc,
                                          CMYK_USWebCoatedSWOP_icc_len);
    } else {
      inType = TYPE_RGBA_8;
      src_profile = cmsCreate_sRGBProfile();
    }
  }

  if (inType == TYPE_CMYK_8 && jinfo->saw_Adobe_marker) {
    inType = TYPE_CMYK_8_REV;
  }

  useTransform = true;

  transform =
      cmsCreateTransform(src_profile, inType, targetProfile, TYPE_RGBA_8,
                         cmsGetHeaderRenderingIntent(src_profile),
                         inType == TYPE_RGBA_8 ? cmsFLAGS_COPY_ALPHA : 0);

  cmsCloseProfile(src_profile);

  jinfo->out_color_space = inType == TYPE_GRAY_8       ? JCS_GRAYSCALE
                           : inType == TYPE_CMYK_8     ? JCS_CMYK
                           : inType == TYPE_CMYK_8_REV ? JCS_CMYK
                                                       : JCS_EXT_RGBA;

  // 8 is the maximum supported scale by libjpeg, so we'll use custom logic for
  // further samples.
  jinfo->scale_denom = std::min(sampleSize, 8u);
  uint32_t customSample = sampleSize <= 8 ? 0 : sampleSize / 8;
  Rect decRect = customSample == 0 ? outRect : outRect.upsample(customSample);

  // libjpeg X axis need to be a multiple of the defined DCT. We need to know
  // these values in order to crop the unwanted region of the final image.
  uint32_t decX = decRect.x;
  uint32_t decWidth = decRect.width;

  // Init decoder and set regions.
  jpeg_start_decompress(jinfo);
  jpeg_crop_scanline(jinfo, &decX, &decWidth);
  jpeg_skip_scanlines(jinfo, decRect.y);

  // Now that we know the decoding width, we can calculate the row stride.
  uint32_t inComponents = jinfo->out_color_space == JCS_GRAYSCALE ? 1 : 4;
  uint32_t inStride = decWidth * inComponents;
  uint32_t inStrideOffset = (decRect.x - decX) * inComponents;

  // Allocate row and get pointers to the row and the row aligned for output
  // image.
  auto inRow = std::vector<uint8_t>(inStride);
  uint8_t* inRowPtr = inRow.data();
  uint8_t* inRowPtrAligned = inRowPtr + inStrideOffset;

  // Save a pointer to the output image and calculate the output stride.
  uint8_t* outPixelsPos = outPixels;
  uint32_t outStride = outRect.width * inComponents;

  if (customSample == 0) {
    // No custom sampler needed, just decode and copy to destination.
    for (uint32_t i = 0; i < outRect.height; i++) {
      jpeg_read_scanlines(jinfo, &inRowPtr, 1);

      memcpy(outPixelsPos, inRowPtrAligned, outStride);

      outPixelsPos += outStride;
    }
  } else {
    // Only RGBA output carries alpha; the fourth CMYK component is black.
    BoxDownsampler downsampler(outRect.width, customSample, inComponents,
                               jinfo->out_color_space == JCS_EXT_RGBA);
    for (uint32_t i = 0; i < outRect.height; i++) {
      for (uint32_t row = 0; row < customSample; ++row) {
        jpeg_read_scanlines(jinfo, &inRowPtr, 1);
        downsampler.addRow(inRowPtrAligned);
      }
      downsampler.writeRow(outPixelsPos);
      outPixelsPos += outStride;
    }
  }

  // Call abort instead of finish to allow finishing before the last scanline.
  jpeg_abort_decompress(jinfo);
}
