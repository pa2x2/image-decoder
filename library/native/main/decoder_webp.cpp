//
// Created by len on 30/12/20.
//

#include "decoder_webp.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <vector>

WebpDecoder::WebpDecoder(std::shared_ptr<Stream>&& stream, bool cropBorders,
                         cmsHPROFILE targetProfile)
    : BaseDecoder(std::move(stream), cropBorders, targetProfile) {
  this->info = parseInfo();
}

ImageInfo WebpDecoder::parseInfo() {
  WebPBitstreamFeatures features;
  if (WebPGetFeatures(stream->bytes, 32, &features) != VP8_STATUS_OK) {
    throw std::runtime_error("Failed to parse webp");
  }

  uint32_t imageWidth = features.width;
  uint32_t imageHeight = features.height;
  bool isAnimated = features.has_animation;

  Rect bounds = {.x = 0, .y = 0, .width = imageWidth, .height = imageHeight};
  if (!isAnimated && cropBorders) {
    int iw = features.width;
    int ih = features.height;
    uint8_t *u, *v;
    int stride, uvStride;
    auto* luma = WebPDecodeYUV(stream->bytes, stream->size, &iw, &ih, &u, &v,
                               &stride, &uvStride);
    if (luma != nullptr) {
      bounds = findBorders(luma, imageWidth, imageHeight);
      WebPFree(luma);
    } else {
      LOGW("Couldn't crop borders on a WebP image of size %dx%d", imageWidth,
           imageHeight);
    }
  }

  return ImageInfo{
      .imageWidth = imageWidth,
      .imageHeight = imageHeight,
      .isAnimated = isAnimated,
      .bounds = bounds,
  };
}

cmsHPROFILE WebpDecoder::getColorProfile() {
  WebPData data = {.bytes = stream->bytes, .size = stream->size};
  WebPDemuxer* const demux = WebPDemux(&data);

  if (!demux) {
    return nullptr;
  }

  uint32_t flags = WebPDemuxGetI(demux, WEBP_FF_FORMAT_FLAGS);
  WebPChunkIterator chunk_iter;
  cmsHPROFILE src_profile = nullptr;

  if ((flags & ICCP_FLAG) && WebPDemuxGetChunk(demux, "ICCP", 1, &chunk_iter)) {
    src_profile =
        cmsOpenProfileFromMem(chunk_iter.chunk.bytes, chunk_iter.chunk.size);

    WebPDemuxReleaseChunkIterator(&chunk_iter);
  }

  WebPDemuxDelete(demux);

  if (!src_profile) {
    return nullptr;
  }

  cmsColorSpaceSignature profileSpace = cmsGetColorSpace(src_profile);

  // WebP doesn't support gray-scale.
  if (profileSpace != cmsSigRgbData) {
    cmsCloseProfile(src_profile);
    return nullptr;
  }

  return src_profile;
}

// libwebp crops a lossy image in its 4:2:0 YUV form: it starts a crop on an odd
// column or row one pixel early, and fills in the chroma at the crop's edges as
// though they were the image's edges. A lossy region decoded at full size is
// therefore decoded widened by a pixel on each side inside the image, from an
// even column and row, which covers every chroma sample its pixels draw on, and
// the region is copied out of that.
static Rect widen_lossy_crop(const Rect& region, uint32_t imageWidth,
                             uint32_t imageHeight) {
  const uint32_t left = (region.x > 0 ? region.x - 1 : 0) & ~1u;
  const uint32_t top = (region.y > 0 ? region.y - 1 : 0) & ~1u;
  const uint32_t right = std::min(imageWidth, region.x + region.width + 1);
  const uint32_t bottom = std::min(imageHeight, region.y + region.height + 1);
  return {.x = left, .y = top, .width = right - left, .height = bottom - top};
}

void WebpDecoder::decode(uint8_t* outPixels, Rect outRect, Rect inRect,
                         uint32_t sampleSize) {
  WebPDecoderConfig config;
  WebPInitDecoderConfig(&config);

  cmsHPROFILE src_profile = getColorProfile();
  if (!src_profile) {
    src_profile = cmsCreate_sRGBProfile();
  }

  useTransform = true;

  inType = TYPE_RGBA_8;

  transform = cmsCreateTransform(
      src_profile, inType, targetProfile, TYPE_RGBA_8,
      cmsGetHeaderRenderingIntent(src_profile), cmsFLAGS_COPY_ALPHA);

  cmsCloseProfile(src_profile);

  // Animated images, whose frames may mix lossy and lossless, count as lossy.
  WebPBitstreamFeatures features;
  const bool lossless = WebPGetFeatures(stream->bytes, stream->size,
                                        &features) == VP8_STATUS_OK &&
                        features.format == 2;

  const bool cropping =
      inRect.width != info.imageWidth || inRect.height != info.imageHeight;
  // A downscaled region is left to libwebp, whose rescaler cannot drop a
  // fraction of an output pixel; an odd crop's one-pixel early start shifts it
  // by less than one output pixel.
  const bool widened = cropping && sampleSize == 1 && !lossless;
  const Rect cropRect =
      widened ? widen_lossy_crop(inRect, info.imageWidth, info.imageHeight)
              : inRect;

  config.options.use_cropping = cropping;
  config.options.crop_left = cropRect.x;
  config.options.crop_top = cropRect.y;
  config.options.crop_width = cropRect.width;
  config.options.crop_height = cropRect.height;

  // Set sample size
  config.options.use_scaling = sampleSize > 1;
  config.options.scaled_width = outRect.width;
  config.options.scaled_height = outRect.height;

  const size_t outStride = static_cast<size_t>(outRect.width) * 4;
  const size_t cropStride = static_cast<size_t>(cropRect.width) * 4;
  std::vector<uint8_t> cropPixels;
  config.output.colorspace = MODE_RGBA;
  if (widened) {
    cropPixels.resize(cropStride * cropRect.height);
    config.output.u.RGBA.rgba = cropPixels.data();
    config.output.u.RGBA.size = cropPixels.size();
    config.output.u.RGBA.stride = static_cast<int>(cropStride);
  } else {
    config.output.u.RGBA.rgba = outPixels;
    config.output.u.RGBA.size = outStride * outRect.height;
    config.output.u.RGBA.stride = static_cast<int>(outStride);
  }
  config.output.is_external_memory = 1;

  VP8StatusCode code;
  if (!info.isAnimated) {
    code = WebPDecode(stream->bytes, stream->size, &config);
  } else {
    WebPData data = {.bytes = stream->bytes, .size = stream->size};
    auto demuxer = std::unique_ptr<WebPDemuxer, decltype(&WebPDemuxDelete)>{
        WebPDemux(&data), WebPDemuxDelete};

    WebPIterator iterator;
    if (!WebPDemuxGetFrame(demuxer.get(), 1, &iterator)) {
      throw std::runtime_error("Failed to init iterator");
    }
    code = WebPDecode(iterator.fragment.bytes, iterator.fragment.size, &config);
    WebPDemuxReleaseIterator(&iterator);
  }
  if (code != VP8_STATUS_OK) {
    throw std::runtime_error("Failed to decode image");
  }

  if (widened) {
    const uint8_t* cropRow = cropPixels.data() +
                             (inRect.y - cropRect.y) * cropStride +
                             static_cast<size_t>(inRect.x - cropRect.x) * 4;
    for (uint32_t row = 0; row < outRect.height; ++row) {
      memcpy(outPixels + row * outStride, cropRow, outStride);
      cropRow += cropStride;
    }
  }
}
