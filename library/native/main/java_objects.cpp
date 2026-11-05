//
// Created by len on 23/12/20.
//

#include "java_objects.h"

static jclass imageTypeCls;
static jmethodID imageTypeCtor;

void init_java_objects(JNIEnv* env) {
  jclass tmpCls = env->FindClass("tachiyomi/decoder/ImageType");
  imageTypeCls = (jclass)env->NewGlobalRef(tmpCls);
  imageTypeCtor = env->GetMethodID(imageTypeCls, "<init>", "(IZ)V");
  env->DeleteLocalRef(tmpCls);
}

jobject create_image_type(JNIEnv* env, jint format, jboolean isAnimated) {
  return env->NewObject(imageTypeCls, imageTypeCtor, format, isAnimated);
}
