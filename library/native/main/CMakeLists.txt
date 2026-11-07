cmake_minimum_required(VERSION 3.24)
project(imagedecoder C CXX ASM)

set(CMAKE_BUILD_TYPE Release)

include(pinned_libraries.cmake)

add_library(imagedecoder SHARED
  alpha_premultiplication.cpp
  pixel_target.cpp
  java_stream.cpp
  java_wrapper.cpp
  java_objects.cpp
  image_format.cpp
  incremental/color_transform.cpp
  incremental/animation_canvas.cpp
  incremental/area_resampling.cpp
  incremental/area_row_resampler.cpp
  incremental/apng_decoder.cpp
  incremental/gif_decoder.cpp
  incremental/image_canvas.cpp
  incremental/session.cpp
  incremental_java_wrapper.cpp
  borders.cpp
  downsampling/box_downsampler.cpp
)

target_include_directories(imagedecoder PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(ANDROID)
  target_sources(imagedecoder PRIVATE pixel_target_android.cpp)
else()
  # The desktop JVM library, with JNI headers from the JDK it builds against.
  if(NOT JAVA_HOME)
    message(FATAL_ERROR "Set JAVA_HOME to the JDK whose JNI headers to build against")
  endif()
  target_sources(imagedecoder PRIVATE pixel_target_jvm.cpp)
  target_include_directories(imagedecoder PRIVATE
    ${JAVA_HOME}/include
    ${JAVA_HOME}/include/linux
  )
endif()

target_link_libraries(imagedecoder PRIVATE pinned_lcms2)

if(WITH_JPEG)
  target_link_libraries(imagedecoder PRIVATE pinned_libjpeg_turbo)
  target_sources(imagedecoder PRIVATE
    decoder_jpeg.cpp
    incremental/jpeg_decoder.cpp
  )
endif()
if(WITH_PNG)
  target_link_libraries(imagedecoder PRIVATE pinned_libpng)
  target_sources(imagedecoder PRIVATE
    decoder_png.cpp
    downsampling/adam7_box_downsampler.cpp
    incremental/png_decoder.cpp
  )
endif()
if(WITH_WEBP)
  target_link_libraries(imagedecoder PRIVATE pinned_libwebp)
  target_sources(imagedecoder PRIVATE
    decoder_webp.cpp
    incremental/animated_webp_decoder.cpp
    incremental/webp_decoder.cpp
  )
endif()
if(WITH_HEIF OR WITH_AVIF)
  target_link_libraries(imagedecoder PRIVATE pinned_libheif)
  target_sources(imagedecoder PRIVATE decoder_heif.cpp)
endif()
if(WITH_JXL)
  target_link_libraries(imagedecoder PRIVATE pinned_libjxl)
  target_sources(imagedecoder PRIVATE
    decoder_jxl.cpp
    incremental/jxl_decoder.cpp
  )
endif()

# Some of the codec libraries carry debug info even in release builds; the
# shipped library leaves it and unused sections out.
target_link_options(imagedecoder PRIVATE -Wl,--gc-sections -Wl,--strip-all)

if(ANDROID)
  target_link_libraries(imagedecoder PRIVATE android jnigraphics log)
else()
  # Only the JNI entry points are exported: the codec libraries built in must
  # never resolve against copies other libraries load into the same process.
  target_link_options(imagedecoder PRIVATE
    -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/jni_exports.map
  )
endif()
