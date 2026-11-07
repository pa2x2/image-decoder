# The image libraries at the versions the shipped library is built with. Each one is fetched, built statically and
# exposed as an INTERFACE target, pinned_<library>, that carries its headers, its link libraries and the
# HAVE_<LIBRARY> definition the decoders test for. The library links these targets and so do its native tests, so the
# tests run against the versions that ship.
include(FetchContent)

# Every dependency is linked into the one shared library that ships.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build the dependencies as shared libraries")

option(WITH_JPEG "Include JPEG decoder" ON)
option(WITH_PNG "Include PNG decoder" ON)
option(WITH_WEBP "Include WebP decoder" ON)
option(WITH_HEIF "Include HEIF decoder" ON)
option(WITH_AVIF "Include AVIF decoder" ON)
option(WITH_JXL "Include JXL decoder" ON)

# Builds of several architectures share one download of each dependency.
get_filename_component(deps "../_deps" REALPATH BASE_DIR "${CMAKE_BINARY_DIR}")
set(FETCHCONTENT_BASE_DIR ${deps})

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/lcms lcms)
if(WITH_JPEG)
  add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/libjpeg-turbo libjpeg-turbo)
endif()
if(WITH_PNG)
  add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/libpng libpng)
endif()
if(WITH_WEBP)
  add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/libwebp libwebp)
endif()
if(WITH_HEIF OR WITH_AVIF)
  add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/libheif libheif)
endif()
if(WITH_JXL)
  add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/libjxl libjxl)
endif()
