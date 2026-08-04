cmake_minimum_required(VERSION 3.22)
project(imagedecoder_host_tests LANGUAGES CXX)

enable_testing()

find_package(JPEG REQUIRED)
find_package(PNG REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LCMS2 REQUIRED IMPORTED_TARGET lcms2)
pkg_check_modules(WEBP REQUIRED IMPORTED_TARGET libwebp)

set(PRODUCTION_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../main/cpp")

add_executable(incremental_decoder_host_tests
  incremental/fixtures/image_pattern.cpp
  incremental/fixtures/jpeg_fixture.cpp
  incremental/fixtures/png_fixture.cpp
  incremental/fixtures/webp_fixture.cpp
  incremental/support/decode_harness.cpp
  incremental/jpeg_decoder_test.cpp
  incremental/png_decoder_test.cpp
  incremental/session_test.cpp
  incremental/test_main.cpp
  incremental/webp_decoder_test.cpp
  ${PRODUCTION_ROOT}/image_format.cpp
  ${PRODUCTION_ROOT}/incremental/color_transform.cpp
  ${PRODUCTION_ROOT}/incremental/image_canvas.cpp
  ${PRODUCTION_ROOT}/incremental/jpeg_decoder.cpp
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
target_compile_definitions(incremental_decoder_host_tests PRIVATE
  HAVE_LIBJPEG
  HAVE_LIBPNG
  HAVE_LIBWEBP
)
target_include_directories(incremental_decoder_host_tests PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/incremental"
  "${CMAKE_CURRENT_SOURCE_DIR}/platform"
  "${PRODUCTION_ROOT}"
)
target_link_libraries(incremental_decoder_host_tests PRIVATE
  JPEG::JPEG
  PNG::PNG
  PkgConfig::LCMS2
  PkgConfig::WEBP
)

add_test(NAME incremental_decoder_host_tests COMMAND incremental_decoder_host_tests)
