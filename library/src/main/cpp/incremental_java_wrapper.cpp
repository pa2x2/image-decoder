#include "alpha_premultiplication.h"
#include "incremental/session.h"
#include "java_objects.h"

#include <android/bitmap.h>
#include <jni.h>
#include <memory>
#include <utility>
#include <vector>

namespace {

constexpr jsize kMaximumDisplayProfileBytes = 4 * 1024 * 1024;
constexpr jint kMaximumOutputDimension = 32768;
constexpr jlong kMaximumBitmapPixels = 67108864;
constexpr jint kMaximumAppendBytes = 64 * 1024;
constexpr jsize kIncrementalUpdateValueCount = 20;

void throw_exception(JNIEnv* env, const char* className, const char* message) {
  const jclass exceptionClass = env->FindClass(className);
  if (exceptionClass != nullptr) {
    env->ThrowNew(exceptionClass, message);
    env->DeleteLocalRef(exceptionClass);
  }
}

void handle_append_result(JNIEnv* env, IncrementalAppendResult result) {
  switch (result) {
  case IncrementalAppendResult::Accepted:
    return;
  case IncrementalAppendResult::InputAlreadyEnded:
    throw_exception(env, "java/lang/IllegalStateException",
                    "End of input has already been signalled");
    return;
  case IncrementalAppendResult::InputTooLarge:
    throw_exception(env, "java/lang/IllegalStateException",
                    "Incremental input exceeds the supported size");
    return;
  }
}

} // namespace

extern "C" JNIEXPORT jlong JNICALL
Java_tachiyomi_decoder_incremental_IncrementalImageDecoder_nativeNewInstance(
    JNIEnv* env, jclass, jint preferredOutputWidth, jlong maximumBitmapPixels,
    jbyteArray displayProfile) {
  if (preferredOutputWidth <= 0 ||
      preferredOutputWidth > kMaximumOutputDimension ||
      maximumBitmapPixels <= 0 || maximumBitmapPixels > kMaximumBitmapPixels) {
    throw_exception(env, "java/lang/IllegalArgumentException",
                    "Incremental decode bounds are invalid");
    return 0;
  }

  std::vector<uint8_t> profile;
  if (displayProfile != nullptr) {
    const jsize profileSize = env->GetArrayLength(displayProfile);
    if (profileSize > kMaximumDisplayProfileBytes) {
      throw_exception(env, "java/lang/IllegalArgumentException",
                      "Display profile exceeds the supported size");
      return 0;
    }
    profile.resize(profileSize);
    if (profileSize > 0) {
      env->GetByteArrayRegion(displayProfile, 0, profileSize,
                              reinterpret_cast<jbyte*>(profile.data()));
      if (env->ExceptionCheck()) {
        return 0;
      }
    }
  }

  auto options = IncrementalDecodeOptionsNative{
      .preferredOutputWidth = static_cast<uint32_t>(preferredOutputWidth),
      .maximumBitmapPixels = static_cast<uint64_t>(maximumBitmapPixels),
      .displayProfile = std::move(profile),
  };
  auto session =
      std::make_unique<IncrementalDecoderSession>(std::move(options));
  return reinterpret_cast<jlong>(session.release());
}

extern "C" JNIEXPORT void JNICALL
Java_tachiyomi_decoder_incremental_IncrementalImageDecoder_nativeAppendByteArray(
    JNIEnv* env, jobject, jlong nativePtr, jbyteArray bytes, jint offset,
    jint length, jboolean endOfInput) {
  auto* session = reinterpret_cast<IncrementalDecoderSession*>(nativePtr);
  if (session == nullptr || bytes == nullptr) {
    throw_exception(env, "java/lang/IllegalStateException",
                    "Incremental decoder is unavailable");
    return;
  }

  const jsize arraySize = env->GetArrayLength(bytes);
  if (offset < 0 || length < 0 || offset > arraySize - length) {
    throw_exception(env, "java/lang/IndexOutOfBoundsException",
                    "Invalid byte array range");
    return;
  }
  if (length > kMaximumAppendBytes) {
    throw_exception(env, "java/lang/IllegalArgumentException",
                    "Incremental input chunk is too large");
    return;
  }

  std::vector<uint8_t> input(static_cast<size_t>(length));
  if (length > 0) {
    env->GetByteArrayRegion(bytes, offset, length,
                            reinterpret_cast<jbyte*>(input.data()));
    if (env->ExceptionCheck()) {
      return;
    }
  }
  handle_append_result(env,
                       session->append(input.empty() ? nullptr : input.data(),
                                       input.size(), endOfInput == JNI_TRUE));
}

extern "C" JNIEXPORT void JNICALL
Java_tachiyomi_decoder_incremental_IncrementalImageDecoder_nativeAppendDirectBuffer(
    JNIEnv* env, jobject, jlong nativePtr, jobject buffer, jint offset,
    jint length, jboolean endOfInput) {
  auto* session = reinterpret_cast<IncrementalDecoderSession*>(nativePtr);
  if (session == nullptr || buffer == nullptr) {
    throw_exception(env, "java/lang/IllegalStateException",
                    "Incremental decoder is unavailable");
    return;
  }

  auto* address = static_cast<uint8_t*>(env->GetDirectBufferAddress(buffer));
  const jlong capacity = env->GetDirectBufferCapacity(buffer);
  if (address == nullptr || capacity < 0) {
    throw_exception(env, "java/lang/IllegalArgumentException",
                    "Expected a direct byte buffer");
    return;
  }
  if (offset < 0 || length < 0 || offset > capacity - length) {
    throw_exception(env, "java/lang/IndexOutOfBoundsException",
                    "Invalid direct buffer range");
    return;
  }
  if (length > kMaximumAppendBytes) {
    throw_exception(env, "java/lang/IllegalArgumentException",
                    "Incremental input chunk is too large");
    return;
  }

  handle_append_result(env, session->append(address + offset,
                                            static_cast<size_t>(length),
                                            endOfInput == JNI_TRUE));
}

extern "C" JNIEXPORT jobject JNICALL
Java_tachiyomi_decoder_incremental_IncrementalImageDecoder_nativePollUpdate(
    JNIEnv* env, jobject, jlong nativePtr, jlongArray updateValues) {
  auto* session = reinterpret_cast<IncrementalDecoderSession*>(nativePtr);
  if (session == nullptr || updateValues == nullptr ||
      env->GetArrayLength(updateValues) < kIncrementalUpdateValueCount) {
    throw_exception(env, "java/lang/IllegalStateException",
                    "Incremental decoder is unavailable");
    return nullptr;
  }

  IncrementalUpdate update{};
  if (!session->pollUpdate(&update)) {
    return nullptr;
  }

  jlong values[kIncrementalUpdateValueCount] = {
      static_cast<jlong>(update.type),
      update.format,
      update.capabilities,
      0,
      0,
      0,
      0,
      0,
      0,
      -1,
      -1,
      0,
      0,
      0,
      0,
      0,
      -1,
      0,
      0,
      0,
  };
  if (update.info != nullptr) {
    values[3] = update.info->width;
    values[4] = update.info->height;
    values[5] = update.info->outputWidth;
    values[6] = update.info->outputHeight;
    values[7] = update.info->isAnimated;
    values[8] = update.info->hasAlpha;
    values[9] = update.info->frameCount;
    values[10] = update.info->loopCount;
  }
  if (update.animationFrame != nullptr) {
    values[16] = update.animationFrame->index;
    values[17] = static_cast<jlong>(update.animationFrame->durationMillis);
    values[18] = static_cast<jlong>(update.animationFrame->blendOperation);
    values[19] = static_cast<jlong>(update.animationFrame->disposalOperation);
  }
  jobject bitmap = nullptr;
  if (update.snapshot != nullptr) {
    values[11] = update.snapshot->generation;
    values[12] = update.snapshot->left;
    values[13] = update.snapshot->top;
    values[14] = update.snapshot->right;
    values[15] = update.snapshot->bottom;

    const size_t expectedSize = static_cast<size_t>(update.snapshot->width) *
                                update.snapshot->height * 4;
    if (update.snapshot->rgba == nullptr ||
        update.snapshot->rgba->size() != expectedSize) {
      throw_exception(env, "java/lang/IllegalStateException",
                      "Incremental bitmap size is invalid");
      return nullptr;
    }
    bitmap =
        create_bitmap(env, update.snapshot->width, update.snapshot->height);
    if (bitmap == nullptr) {
      throw_exception(env, "java/lang/OutOfMemoryError",
                      "Failed to allocate incremental bitmap");
      return nullptr;
    }

    AndroidBitmapInfo bitmapInfo{};
    void* bitmapPixels = nullptr;
    const size_t sourceStride = static_cast<size_t>(update.snapshot->width) * 4;
    if (AndroidBitmap_getInfo(env, bitmap, &bitmapInfo) !=
            ANDROID_BITMAP_RESULT_SUCCESS ||
        bitmapInfo.stride < sourceStride ||
        AndroidBitmap_lockPixels(env, bitmap, &bitmapPixels) !=
            ANDROID_BITMAP_RESULT_SUCCESS ||
        bitmapPixels == nullptr) {
      env->DeleteLocalRef(bitmap);
      throw_exception(env, "java/lang/IllegalStateException",
                      "Failed to access incremental bitmap pixels");
      return nullptr;
    }
    // Snapshots stay straight so later updates can keep compositing and
    // resampling them; only the bitmap copy is premultiplied.
    for (uint32_t row = 0; row < update.snapshot->height; ++row) {
      premultiply_rgba(update.snapshot->rgba->data() + row * sourceStride,
                       static_cast<uint8_t*>(bitmapPixels) +
                           static_cast<size_t>(row) * bitmapInfo.stride,
                       update.snapshot->width);
    }
    AndroidBitmap_unlockPixels(env, bitmap);
  }
  env->SetLongArrayRegion(updateValues, 0, kIncrementalUpdateValueCount,
                          values);
  if (env->ExceptionCheck()) {
    if (bitmap != nullptr) {
      env->DeleteLocalRef(bitmap);
    }
    return nullptr;
  }
  return bitmap;
}

extern "C" JNIEXPORT void JNICALL
Java_tachiyomi_decoder_incremental_IncrementalImageDecoder_nativeRecycle(
    JNIEnv*, jobject, jlong nativePtr) {
  delete reinterpret_cast<IncrementalDecoderSession*>(nativePtr);
}
