# zlib for libpng in the desktop library; Android has it in the system. libpng
# asks for it with find_package(ZLIB), which this answers with the static
# library built here, and reads FindZLIB's variables as well as its target.
FetchContent_Declare(zlib
  GIT_REPOSITORY  https://github.com/madler/zlib
  GIT_TAG         v1.3.2
  BINARY_DIR      zlib-build
  SUBBUILD_DIR    zlib-subbuild
  OVERRIDE_FIND_PACKAGE
)

option(ZLIB_BUILD_SHARED "" OFF)
option(ZLIB_BUILD_TESTING "" OFF)

FetchContent_MakeAvailable(zlib)

add_library(ZLIB::ZLIB ALIAS zlibstatic)
set(ZLIB_FOUND TRUE)
set(ZLIB_INCLUDE_DIRS ${zlib_SOURCE_DIR} ${zlib_BINARY_DIR})
set(ZLIB_LIBRARIES ZLIB::ZLIB)
