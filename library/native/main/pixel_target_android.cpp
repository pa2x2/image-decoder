#include "pixel_target.h"

#include <android/bitmap.h>

void init_platform_pixel_targets(JNIEnv*) {}

bool lock_pixel_target(JNIEnv* env, jobject target, uint32_t width,
                       uint32_t height, LockedPixels* locked) {
  AndroidBitmapInfo info{};
  if (AndroidBitmap_getInfo(env, target, &info) !=
          ANDROID_BITMAP_RESULT_SUCCESS ||
      info.format != ANDROID_BITMAP_FORMAT_RGBA_8888 || info.width < width ||
      info.height < height || info.stride < static_cast<size_t>(width) * 4) {
    return false;
  }
  void* pixels = nullptr;
  if (AndroidBitmap_lockPixels(env, target, &pixels) !=
          ANDROID_BITMAP_RESULT_SUCCESS ||
      pixels == nullptr) {
    return false;
  }
  locked->pixels = static_cast<uint8_t*>(pixels);
  locked->stride = info.stride;
  return true;
}

void unlock_pixel_target(JNIEnv* env, jobject target) {
  AndroidBitmap_unlockPixels(env, target);
}
