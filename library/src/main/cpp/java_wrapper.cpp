//
// Created by len on 23/12/20.
//

#include "alpha_premultiplication.h"
#include "borders.h"
#include "decoder_base.h"
#include "decoders.h"
#include "java_objects.h"
#include "java_stream.h"
#include <android/bitmap.h>
#include <jni.h>
#include <lcms2.h>
#include <vector>

jint JNI_OnLoad(JavaVM* vm, void*) {
  JNIEnv* env;
  if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_OK) {
    init_java_stream(env);
    init_java_objects(env);

  } else {
    return JNI_ERR;
  }
  return JNI_VERSION_1_6;
}

extern "C" JNIEXPORT jobject JNICALL
Java_tachiyomi_decoder_ImageDecoder_nativeNewInstance(JNIEnv* env, jclass,
                                                      jobject jstream,
                                                      jboolean cropBorders,
                                                      jbyteArray icm_stream) {
  auto stream = read_all_java_stream(env, jstream);
  if (!stream) {
    return nullptr;
  }

  cmsHPROFILE targetProfile = nullptr;
  if (icm_stream) {
    int icm_stream_len = env->GetArrayLength(icm_stream);
    if (icm_stream_len > 0) {
      std::vector<uint8_t> icm_buf(icm_stream_len);
      env->GetByteArrayRegion(icm_stream, 0, icm_stream_len,
                              reinterpret_cast<jbyte*>(icm_buf.data()));

      targetProfile = cmsOpenProfileFromMem(icm_buf.data(), icm_buf.size());
    }
  }

  if (!targetProfile) {
    targetProfile = cmsCreate_sRGBProfile();
  }

  BaseDecoder* decoder;
  try {
    if (false) {
    } // This should be optimized out by the compiler.
#ifdef HAVE_LIBJPEG
    else if (is_jpeg(stream->bytes, stream->size)) {
      decoder = new JpegDecoder(std::move(stream), cropBorders, targetProfile);
    }
#endif
#ifdef HAVE_LIBPNG
    else if (is_png(stream->bytes, stream->size)) {
      decoder = new PngDecoder(std::move(stream), cropBorders, targetProfile);
    }
#endif
#ifdef HAVE_LIBWEBP
    else if (is_webp(stream->bytes, stream->size)) {
      decoder = new WebpDecoder(std::move(stream), cropBorders, targetProfile);
    }
#endif
#ifdef HAVE_LIBHEIF
    else if (is_libheif_compatible(stream->bytes, stream->size)) {
      decoder = new HeifDecoder(std::move(stream), cropBorders, targetProfile);
    }
#endif
#ifdef HAVE_LIBJXL
    else if (is_jxl(stream->bytes, stream->size)) {
      decoder =
          new JpegxlDecoder(std::move(stream), cropBorders, targetProfile);
    }
#endif
    else {
      LOGE("No decoder found to handle this stream");
      return nullptr;
    }
  } catch (std::exception& ex) {
    LOGE("%s", ex.what());
    return nullptr;
  }

  const ImageInfo& info = decoder->info;
  return create_image_decoder(env, (jlong)decoder, info.imageWidth,
                              info.imageHeight, info.bounds.x, info.bounds.y,
                              info.bounds.width, info.bounds.height);
}

extern "C" JNIEXPORT jobject JNICALL
Java_tachiyomi_decoder_ImageDecoder_nativeDecode(JNIEnv* env, jobject,
                                                 jlong decoderPtr,
                                                 jint sampleSize, jint x,
                                                 jint y, jint width,
                                                 jint height) {
  auto* decoder = (BaseDecoder*)decoderPtr;

  // Bounds of the image when crop borders is enabled, otherwise it matches the
  // entire image.
  Rect bounds = decoder->info.bounds;

  // Translated requested bounds to the original image.
  Rect inRect = {x + bounds.x, y + bounds.y, (uint32_t)width, (uint32_t)height};

  // Sampled requested bounds according to sampleSize.
  // It matches the translated bounds when the value is 1
  Rect outRect = inRect.downsample(sampleSize);
  if (outRect.width == 0 || outRect.height == 0) {
    LOGE("Requested sample size too high");
    return nullptr;
  }

  auto* bitmap = create_bitmap(env, outRect.width, outRect.height);
  if (!bitmap) {
    LOGE("Failed to create a bitmap of size %dx%dx%d", outRect.width,
         outRect.height, 4);
    return nullptr;
  }

  uint8_t* pixels;
  AndroidBitmap_lockPixels(env, bitmap, (void**)&pixels);
  if (!pixels) {
    LOGE("Failed to lock pixels");
    return nullptr;
  }

  try {
    const size_t pixelCount =
        static_cast<size_t>(outRect.width) * outRect.height;
    std::vector<uint8_t> out_buffer(pixelCount * 4);
    uint8_t* pout_buffer = out_buffer.data();

    decoder->decode(pout_buffer, outRect, inRect, sampleSize);

    // Decoding and color management use straight alpha; the bitmap is
    // premultiplied.
    if (decoder->useTransform) {
      cmsDoTransform(decoder->transform, pout_buffer, pixels, pixelCount);

      if (decoder->inType == TYPE_CMYK_8 ||
          decoder->inType == TYPE_CMYK_8_REV ||
          decoder->inType == TYPE_GRAY_8) {
        for (size_t i = 0; i < pixelCount; i++) {
          pixels[i * 4 + 3] = 255;
        }
      } else {
        premultiply_rgba(pixels, pixels, pixelCount);
      }
    } else {
      // out_buffer must be straight rgba.
      premultiply_rgba(pout_buffer, pixels, pixelCount);
    }
  } catch (std::exception& ex) {
    LOGE("%s", ex.what());
    AndroidBitmap_unlockPixels(env, bitmap);
    return nullptr;
  }

  AndroidBitmap_unlockPixels(env, bitmap);
  return bitmap;
}

extern "C" JNIEXPORT void JNICALL
Java_tachiyomi_decoder_ImageDecoder_nativeRecycle(JNIEnv*, jobject,
                                                  jlong decoderPtr) {
  auto* decoder = (BaseDecoder*)decoderPtr;
  delete decoder;
}

extern "C" JNIEXPORT jobject JNICALL
Java_tachiyomi_decoder_ImageDecoder_nativeFindType(JNIEnv* env, jclass,
                                                   jbyteArray array) {
  constexpr uint32_t maximumSniffBytes = 4096;
  uint32_t size =
      std::min<uint32_t>(env->GetArrayLength(array), maximumSniffBytes);

  if (size == 0) {
    return nullptr;
  }

  auto _bytes = std::make_unique<uint8_t[]>(size);
  auto bytes = _bytes.get();
  env->GetByteArrayRegion(array, 0, size, (jbyte*)bytes);

  if (is_jpeg(bytes, size)) {
    return create_image_type(env, 0, false);
  } else if (is_png(bytes, size)) {
    return create_image_type(env, 1, is_animated_png(bytes, size));
  } else if (is_webp(bytes, size)) {
    return create_image_type(env, 2, is_animated_webp(bytes, size));
  } else if (is_gif(bytes, size)) {
    return create_image_type(env, 3, true);
  } else if (is_jxl(bytes, size)) {
    return create_image_type(env, 6, false);
  } else if (is_heif(bytes, size)) {
    return create_image_type(env, 4, false);
  } else if (is_avif(bytes, size)) {
    return create_image_type(env, 5, false);
  }

  LOGW("Failed to find image type");
  return nullptr;
}
