if(NOT ANDROID)
  include(zlib.cmake)
endif()

FetchContent_Declare(libpng
  GIT_REPOSITORY  https://github.com/glennrp/libpng
  GIT_TAG         v1.6.42
  BINARY_DIR      build
  SUBBUILD_DIR    subbuild
)

option(PNG_SHARED "" OFF)
option(PNG_TOOLS "" OFF)
option(PNG_TESTS "" OFF)

FetchContent_MakeAvailable(libpng)

add_library(pinned_libpng INTERFACE)
target_include_directories(pinned_libpng SYSTEM INTERFACE ${libpng_BINARY_DIR} ${libpng_SOURCE_DIR})
target_link_libraries(pinned_libpng INTERFACE png_static)
target_compile_definitions(pinned_libpng INTERFACE HAVE_LIBPNG)
