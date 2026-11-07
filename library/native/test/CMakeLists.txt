cmake_minimum_required(VERSION 3.22)
project(imagedecoder_host_tests LANGUAGES C CXX ASM)

enable_testing()

set(PRODUCTION_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../main")

# The decoders under test link the libraries the shipped library pins, not the
# host's. The HEIF decoder has no host tests, so libheif is left out.
set(WITH_HEIF OFF)
set(WITH_AVIF OFF)
include(${PRODUCTION_ROOT}/pinned_libraries.cmake)

# The fixtures encode their images, so they also need the encoders that the
# shipped library leaves out.
set(FIXTURE_ENCODERS jxl webp libwebpmux)

add_executable(incremental_decoder_host_tests
  incremental/fixtures/image_pattern.cpp
  incremental/fixtures/gif_fixture.cpp
  incremental/fixtures/jpeg_fixture.cpp
  incremental/fixtures/jxl_fixture.cpp
  incremental/fixtures/png_fixture.cpp
  incremental/fixtures/webp_fixture.cpp
  incremental/support/decode_harness.cpp
  incremental/support/scaled_decode_expectations.cpp
  incremental/area_resampling_test.cpp
  incremental/jpeg_decoder_test.cpp
  incremental/gif_decoder_test.cpp
  incremental/jxl_decoder_test.cpp
  incremental/png_decoder_test.cpp
  incremental/session_test.cpp
  incremental/test_main.cpp
  incremental/webp_decoder_test.cpp
  ${PRODUCTION_ROOT}/image_format.cpp
  ${PRODUCTION_ROOT}/incremental/color_transform.cpp
  ${PRODUCTION_ROOT}/incremental/animated_webp_decoder.cpp
  ${PRODUCTION_ROOT}/incremental/animation_canvas.cpp
  ${PRODUCTION_ROOT}/incremental/area_resampling.cpp
  ${PRODUCTION_ROOT}/incremental/area_row_resampler.cpp
  ${PRODUCTION_ROOT}/incremental/apng_decoder.cpp
  ${PRODUCTION_ROOT}/incremental/gif_decoder.cpp
  ${PRODUCTION_ROOT}/incremental/image_canvas.cpp
  ${PRODUCTION_ROOT}/incremental/jpeg_decoder.cpp
  ${PRODUCTION_ROOT}/incremental/jxl_decoder.cpp
  ${PRODUCTION_ROOT}/incremental/png_decoder.cpp
  ${PRODUCTION_ROOT}/incremental/session.cpp
  ${PRODUCTION_ROOT}/incremental/webp_decoder.cpp
)

target_compile_features(incremental_decoder_host_tests PRIVATE cxx_std_20)
target_compile_options(incremental_decoder_host_tests PRIVATE
  -Wall
  -Wextra
  -Wpedantic
  -Werror
)
target_include_directories(incremental_decoder_host_tests PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/incremental"
  "${PRODUCTION_ROOT}"
)
target_link_libraries(incremental_decoder_host_tests PRIVATE
  pinned_libjpeg_turbo
  pinned_libjxl
  pinned_libpng
  pinned_lcms2
  pinned_libwebp
  ${FIXTURE_ENCODERS}
)

add_test(NAME incremental_decoder_host_tests COMMAND incremental_decoder_host_tests)

find_package(JNI REQUIRED)

add_executable(decoder_host_tests
  alpha_premultiplication_test.cpp
  downsampling/box_block_sums_test.cpp
  downsampling/box_downsampler_test.cpp
  png_border_detection_test.cpp
  sampled_decode_memory_test.cpp
  sampled_decode_test.cpp
  straight_alpha_decode_test.cpp
  support/peak_allocation.cpp
  test_main.cpp
  incremental/fixtures/image_pattern.cpp
  incremental/fixtures/jpeg_fixture.cpp
  incremental/fixtures/jxl_fixture.cpp
  incremental/fixtures/png_fixture.cpp
  incremental/support/decode_harness.cpp
  ${PRODUCTION_ROOT}/alpha_premultiplication.cpp
  ${PRODUCTION_ROOT}/downsampling/adam7_box_downsampler.cpp
  ${PRODUCTION_ROOT}/downsampling/box_downsampler.cpp
)

# The regular decoders predate the host tests and are held to the Android
# build's warning settings, not this suite's stricter ones.
add_library(regular_decoders OBJECT
  ${PRODUCTION_ROOT}/borders.cpp
  ${PRODUCTION_ROOT}/decoder_jpeg.cpp
  ${PRODUCTION_ROOT}/decoder_jxl.cpp
  ${PRODUCTION_ROOT}/decoder_png.cpp
)
target_compile_features(regular_decoders PRIVATE cxx_std_20)
target_include_directories(regular_decoders PRIVATE
  "${PRODUCTION_ROOT}"
  ${JNI_INCLUDE_DIRS}
)
target_link_libraries(regular_decoders PRIVATE
  pinned_libjpeg_turbo
  pinned_libjxl
  pinned_libpng
  pinned_lcms2
)

target_compile_features(decoder_host_tests PRIVATE cxx_std_20)
target_compile_options(decoder_host_tests PRIVATE
  -Wall
  -Wextra
  -Wpedantic
  -Werror
)
target_include_directories(decoder_host_tests PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/incremental"
  "${PRODUCTION_ROOT}"
  ${JNI_INCLUDE_DIRS}
)
target_link_libraries(decoder_host_tests PRIVATE
  regular_decoders
  pinned_libjpeg_turbo
  pinned_libjxl
  pinned_libpng
  pinned_lcms2
  ${FIXTURE_ENCODERS}
)

add_test(NAME decoder_host_tests COMMAND decoder_host_tests)
