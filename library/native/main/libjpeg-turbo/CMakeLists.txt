FetchContent_Declare(libjpeg-turbo
  GIT_REPOSITORY  https://github.com/libjpeg-turbo/libjpeg-turbo
  GIT_TAG         2.1.90
  BINARY_DIR      build
  SUBBUILD_DIR    subbuild
)

option(WITH_JPEG8 "" 1)
option(WITH_TURBOJPEG "" 0)
option(ENABLE_SHARED "" 0)
option(REQUIRE_SIMD "" 1)

FetchContent_MakeAvailable(libjpeg-turbo)

add_library(pinned_libjpeg_turbo INTERFACE)
target_include_directories(pinned_libjpeg_turbo SYSTEM INTERFACE ${libjpeg-turbo_BINARY_DIR} ${libjpeg-turbo_SOURCE_DIR})
target_link_libraries(pinned_libjpeg_turbo INTERFACE jpeg-static)
target_compile_definitions(pinned_libjpeg_turbo INTERFACE HAVE_LIBJPEG)
