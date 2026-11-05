#ifndef IMAGEDECODER_PIXEL_TARGET_H
#define IMAGEDECODER_PIXEL_TARGET_H

#include <cstddef>
#include <cstdint>
#include <jni.h>

// Decoded pixels are written into an object the Kotlin side allocates through
// a tachiyomi.decoder.PixelAllocator: an android.graphics.Bitmap on Android
// (pixel_target_android.cpp), a caller-owned buffer described by a
// tachiyomi.decoder.PixelTarget on the JVM (pixel_target_jvm.cpp). Either way
// the pixels are interleaved 8-bit RGBA with premultiplied alpha.

struct LockedPixels {
  uint8_t* pixels;
  // Bytes from the start of one row to the start of the next.
  size_t stride;
};

void init_pixel_targets(JNIEnv* env);

// Asks the allocator for a target of the given size. Returns null when the
// allocator has none to give, or when it threw, leaving its exception pending
// for the Kotlin caller.
jobject allocate_pixel_target(JNIEnv* env, jobject allocator, uint32_t width,
                              uint32_t height);

// Exposes the target's pixels for writing until unlock_pixel_target. Fails
// when the target does not hold at least width x height RGBA pixels.
bool lock_pixel_target(JNIEnv* env, jobject target, uint32_t width,
                       uint32_t height, LockedPixels* locked);

void unlock_pixel_target(JNIEnv* env, jobject target);

// Implemented per platform, next to lock_pixel_target; called by
// init_pixel_targets.
void init_platform_pixel_targets(JNIEnv* env);

#endif // IMAGEDECODER_PIXEL_TARGET_H
