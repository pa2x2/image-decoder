#include "pixel_target.h"

static jmethodID addressMethod;
static jmethodID rowBytesMethod;
static jmethodID widthMethod;
static jmethodID heightMethod;

void init_platform_pixel_targets(JNIEnv* env) {
  jclass targetCls = env->FindClass("tachiyomi/decoder/PixelTarget");
  addressMethod = env->GetMethodID(targetCls, "getAddress", "()J");
  rowBytesMethod = env->GetMethodID(targetCls, "getRowBytes", "()J");
  widthMethod = env->GetMethodID(targetCls, "getWidth", "()I");
  heightMethod = env->GetMethodID(targetCls, "getHeight", "()I");
  env->DeleteLocalRef(targetCls);
}

bool lock_pixel_target(JNIEnv* env, jobject target, uint32_t width,
                       uint32_t height, LockedPixels* locked) {
  const jlong address = env->CallLongMethod(target, addressMethod);
  const jlong rowBytes = env->CallLongMethod(target, rowBytesMethod);
  const jint targetWidth = env->CallIntMethod(target, widthMethod);
  const jint targetHeight = env->CallIntMethod(target, heightMethod);
  if (env->ExceptionCheck()) {
    env->ExceptionClear();
    return false;
  }
  if (address == 0 || targetWidth < 0 || targetHeight < 0 ||
      static_cast<uint32_t>(targetWidth) < width ||
      static_cast<uint32_t>(targetHeight) < height || rowBytes < 0 ||
      static_cast<uint64_t>(rowBytes) < static_cast<uint64_t>(width) * 4) {
    return false;
  }
  locked->pixels = reinterpret_cast<uint8_t*>(address);
  locked->stride = static_cast<size_t>(rowBytes);
  return true;
}

// The caller owns the memory and keeps it alive while the decode runs.
void unlock_pixel_target(JNIEnv*, jobject) {}
