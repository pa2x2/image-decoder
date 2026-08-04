#ifndef IMAGEDECODER_HOST_TEST_ANDROID_LOG_H
#define IMAGEDECODER_HOST_TEST_ANDROID_LOG_H

#define ANDROID_LOG_VERBOSE 2
#define ANDROID_LOG_DEBUG 3
#define ANDROID_LOG_INFO 4
#define ANDROID_LOG_WARN 5
#define ANDROID_LOG_ERROR 6
#define ANDROID_LOG_FATAL 7

inline int __android_log_print(int, const char*, const char*, ...) { return 0; }

#endif // IMAGEDECODER_HOST_TEST_ANDROID_LOG_H
