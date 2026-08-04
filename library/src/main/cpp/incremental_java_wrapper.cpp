#include "incremental_decoder.h"

#include <jni.h>
#include <memory>
#include <utility>
#include <vector>

namespace {

constexpr jsize kMaximumDisplayProfileBytes = 4 * 1024 * 1024;
constexpr jint kMaximumOutputDimension = 32768;
constexpr jlong kMaximumBitmapPixels = 67108864;
constexpr jint kMaximumAppendBytes = 64 * 1024;

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

extern "C" JNIEXPORT jintArray JNICALL
Java_tachiyomi_decoder_incremental_IncrementalImageDecoder_nativePollUpdate(
    JNIEnv* env, jobject, jlong nativePtr) {
  auto* session = reinterpret_cast<IncrementalDecoderSession*>(nativePtr);
  if (session == nullptr) {
    throw_exception(env, "java/lang/IllegalStateException",
                    "Incremental decoder is unavailable");
    return nullptr;
  }

  IncrementalUpdate update{};
  if (!session->pollUpdate(&update)) {
    return nullptr;
  }

  const jint values[] = {
      static_cast<jint>(update.type),
      static_cast<jint>(update.format),
      static_cast<jint>(update.capabilities),
  };
  jintArray result = env->NewIntArray(3);
  if (result != nullptr) {
    env->SetIntArrayRegion(result, 0, 3, values);
  }
  return result;
}

extern "C" JNIEXPORT void JNICALL
Java_tachiyomi_decoder_incremental_IncrementalImageDecoder_nativeRecycle(
    JNIEnv*, jobject, jlong nativePtr) {
  delete reinterpret_cast<IncrementalDecoderSession*>(nativePtr);
}
