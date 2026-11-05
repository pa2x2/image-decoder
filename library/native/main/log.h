#ifndef LOG_H
#define LOG_H

#define TAG "ImageDecoder"

#ifdef __ANDROID__

#include <android/log.h>

#define LOGV(...) __android_log_print(ANDROID_LOG_VERBOSE, TAG, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)
#define LOGF(...) __android_log_print(ANDROID_LOG_FATAL, TAG, __VA_ARGS__)

#else

// Outside Android there is no system log the host process reads, so warnings
// and errors go to stderr and the chattier levels are dropped.
#include <cstdio>

#define IMAGEDECODER_LOG_STDERR(level, ...)                                    \
  do {                                                                         \
    std::fprintf(stderr, "%s %s: ", level, TAG);                               \
    std::fprintf(stderr, __VA_ARGS__);                                         \
    std::fputc('\n', stderr);                                                  \
  } while (0)

#define LOGV(...) ((void)0)
#define LOGD(...) ((void)0)
#define LOGI(...) ((void)0)
#define LOGW(...) IMAGEDECODER_LOG_STDERR("W", __VA_ARGS__)
#define LOGE(...) IMAGEDECODER_LOG_STDERR("E", __VA_ARGS__)
#define LOGF(...) IMAGEDECODER_LOG_STDERR("F", __VA_ARGS__)

#endif

#endif // LOG_H
