FetchContent_Declare(libwebp
  GIT_REPOSITORY  https://chromium.googlesource.com/webm/libwebp
  GIT_TAG         v1.3.2
  BINARY_DIR      build
  SUBBUILD_DIR    subbuild
)

option(WEBP_BUILD_ANIM_UTILS "" OFF)
option(WEBP_BUILD_CWEBP "" OFF)
option(WEBP_BUILD_DWEBP "" OFF)
option(WEBP_BUILD_GIF2WEBP "" OFF)
option(WEBP_BUILD_IMG2WEBP "" OFF)
option(WEBP_BUILD_VWEBP "" OFF)
option(WEBP_BUILD_WEBPINFO "" OFF)
option(WEBP_BUILD_WEBPMUX "" OFF)
option(WEBP_BUILD_EXTRAS "" OFF)
option(WEBP_BUILD_WEBP_JS "" OFF)
option(WEBP_ENABLE_SWAP_16BIT_CSP "" ON)

FetchContent_MakeAvailable(libwebp)

add_library(pinned_libwebp INTERFACE)
target_include_directories(pinned_libwebp SYSTEM INTERFACE ${libwebp_BINARY_DIR} ${libwebp_SOURCE_DIR})
target_link_libraries(pinned_libwebp INTERFACE webpdecoder webpdemux)
target_compile_definitions(pinned_libwebp INTERFACE HAVE_LIBWEBP)
