cmake_minimum_required(VERSION 3.14)
project(imagedecoder C CXX ASM)
include(FetchContent)

set(CMAKE_BUILD_TYPE Release)

option(WITH_JPEG "Include JPEG decoder" ON)
option(WITH_PNG "Include PNG decoder" ON)
option(WITH_WEBP "Include WebP decoder" ON)
option(WITH_HEIF "Include HEIF decoder" ON)
option(WITH_AVIF "Include AVIF decoder" ON)
option(WITH_JXL "Include JXL decoder" ON)

add_library(imagedecoder SHARED
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

# Change the default path of fetch content to avoid downloading the same dependencies per architecture
get_filename_component(deps "../_deps" REALPATH BASE_DIR "${CMAKE_BINARY_DIR}")
set(FETCHCONTENT_BASE_DIR ${deps})

add_subdirectory(lcms)

if(WITH_JPEG)
  add_subdirectory(libjpeg-turbo)
  add_definitions(-DHAVE_LIBJPEG)
  target_sources(imagedecoder PRIVATE
    decoder_jpeg.cpp
    incremental/jpeg_decoder.cpp
  )
endif()
if(WITH_PNG)
  add_subdirectory(libpng)
  add_definitions(-DHAVE_LIBPNG)
  target_sources(imagedecoder PRIVATE
    decoder_png.cpp
    downsampling/adam7_box_downsampler.cpp
    incremental/png_decoder.cpp
  )
endif()
if(WITH_WEBP)
  add_subdirectory(libwebp)
  add_definitions(-DHAVE_LIBWEBP)
  target_sources(imagedecoder PRIVATE
    decoder_webp.cpp
    incremental/animated_webp_decoder.cpp
    incremental/webp_decoder.cpp
  )
endif()
if(WITH_HEIF OR WITH_AVIF)
  add_subdirectory(libheif)
  add_definitions(-DHAVE_LIBHEIF)
  target_sources(imagedecoder PRIVATE decoder_heif.cpp)
endif()
if(WITH_JXL)
  add_subdirectory(libjxl)
  add_definitions(-DHAVE_LIBJXL)
  target_sources(imagedecoder PRIVATE
    decoder_jxl.cpp
    incremental/jxl_decoder.cpp
  )
endif()

target_link_libraries(imagedecoder android jnigraphics log)
