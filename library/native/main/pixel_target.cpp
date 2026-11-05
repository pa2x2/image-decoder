#include "pixel_target.h"

static jmethodID allocateMethod;

void init_pixel_targets(JNIEnv* env) {
  jclass allocatorCls = env->FindClass("tachiyomi/decoder/PixelAllocator");
  allocateMethod =
      env->GetMethodID(allocatorCls, "allocate", "(II)Ljava/lang/Object;");
  env->DeleteLocalRef(allocatorCls);

  init_platform_pixel_targets(env);
}

jobject allocate_pixel_target(JNIEnv* env, jobject allocator, uint32_t width,
                              uint32_t height) {
  jobject target = env->CallObjectMethod(allocator, allocateMethod,
                                         static_cast<jint>(width),
                                         static_cast<jint>(height));
  return env->ExceptionCheck() ? nullptr : target;
}
