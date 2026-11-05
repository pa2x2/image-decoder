//
// Created by len on 23/12/20.
//

#include "alpha_premultiplication.h"
#include "borders.h"
#include "decoder_base.h"
#include "decoders.h"
#include "java_objects.h"
#include "java_stream.h"
#include "pixel_target.h"
#include <jni.h>
#include <lcms2.h>
#include <vector>

jint JNI_OnLoad(JavaVM* vm, void*) {
  JNIEnv* env;
  if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_OK) {
    init_java_stream(env);
    init_java_objects(env);
    init_pixel_targets(env);

  } else {
    return JNI_ERR;
  }
  return JNI_VERSION_1_6;
}

// Values nativeNewInstance writes into its info array, in this order.
constexpr jsize kDecoderInfoCount = 6;

extern "C" JNIEXPORT jlong JNICALL
Java_tachiyomi_decoder_NativeImageDecoder_nativeNewInstance(
    JNIEnv* env, jclass, jobject jstream, jboolean cropBorders,
    jbyteArray icm_stream, jintArray infoArray) {
  if (env->GetArrayLength(infoArray) < kDecoderInfoCount) {
    return 0;
  }
  auto stream = read_all_java_stream(env, jstream);
  if (!stream) {
    return 0;
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
      return 0;
    }
  } catch (std::exception& ex) {
    LOGE("%s", ex.what());
    return 0;
  }

  const ImageInfo& info = decoder->info;
  const jint values[kDecoderInfoCount] = {
      static_cast<jint>(info.imageWidth),   static_cast<jint>(info.imageHeight),
      static_cast<jint>(info.bounds.x),     static_cast<jint>(info.bounds.y),
      static_cast<jint>(info.bounds.width), static_cast<jint>(info.bounds.height),
  };
  env->SetIntArrayRegion(infoArray, 0, kDecoderInfoCount, values);
  return reinterpret_cast<jlong>(decoder);
}

extern "C" JNIEXPORT jobject JNICALL
Java_tachiyomi_decoder_NativeImageDecoder_nativeDecode(
    JNIEnv* env, jclass, jlong decoderPtr, jint sampleSize, jint x, jint y,
    jint width, jint height, jobject allocator) {
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

  jobject target =
      allocate_pixel_target(env, allocator, outRect.width, outRect.height);
  if (!target) {
    if (!env->ExceptionCheck()) {
      LOGE("Failed to allocate pixels of size %dx%d", outRect.width,
           outRect.height);
    }
    return nullptr;
  }

  LockedPixels locked{};
  if (!lock_pixel_target(env, target, outRect.width, outRect.height,
                         &locked)) {
    LOGE("Failed to lock pixels");
    env->DeleteLocalRef(target);
    return nullptr;
  }

  try {
    const size_t pixelCount =
        static_cast<size_t>(outRect.width) * outRect.height;
    std::vector<uint8_t> out_buffer(pixelCount * 4);
    uint8_t* pout_buffer = out_buffer.data();

    decoder->decode(pout_buffer, outRect, inRect, sampleSize);

    // Decoding and color management use straight alpha; the target is
    // premultiplied. Rows are written one at a time, as the target's rows may
    // be padded.
    const size_t rowBytes = static_cast<size_t>(outRect.width) * 4;
    const bool opaqueTransform = decoder->inType == TYPE_CMYK_8 ||
                                 decoder->inType == TYPE_CMYK_8_REV ||
                                 decoder->inType == TYPE_GRAY_8;
    for (uint32_t row = 0; row < outRect.height; row++) {
      const uint8_t* source = pout_buffer + row * rowBytes;
      uint8_t* destination = locked.pixels + row * locked.stride;
      if (decoder->useTransform) {
        cmsDoTransform(decoder->transform, source, destination, outRect.width);
        if (opaqueTransform) {
          for (uint32_t i = 0; i < outRect.width; i++) {
            destination[i * 4 + 3] = 255;
          }
        } else {
          premultiply_rgba(destination, destination, outRect.width);
        }
      } else {
        premultiply_rgba(source, destination, outRect.width);
      }
    }
  } catch (std::exception& ex) {
    LOGE("%s", ex.what());
    unlock_pixel_target(env, target);
    env->DeleteLocalRef(target);
    return nullptr;
  }

  unlock_pixel_target(env, target);
  return target;
}

extern "C" JNIEXPORT void JNICALL
Java_tachiyomi_decoder_NativeImageDecoder_nativeRecycle(JNIEnv*, jclass,
                                                        jlong decoderPtr) {
  auto* decoder = (BaseDecoder*)decoderPtr;
  delete decoder;
}

extern "C" JNIEXPORT jobject JNICALL
Java_tachiyomi_decoder_NativeImageDecoder_nativeFindType(JNIEnv* env, jclass,
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
