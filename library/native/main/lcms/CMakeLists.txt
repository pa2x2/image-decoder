FetchContent_Declare(lcms2
  GIT_REPOSITORY  https://github.com/mm2/Little-CMS
  GIT_TAG         lcms2.20
  BINARY_DIR      build
  SUBBUILD_DIR    subbuild
)

option(LCMS2_BUILD_SHARED "" OFF)
option(LCMS2_BUILD_TOOLS "" OFF)
option(LCMS2_BUILD_TESTS "" OFF)

FetchContent_MakeAvailable(lcms2)

add_library(pinned_lcms2 INTERFACE)
target_include_directories(pinned_lcms2 SYSTEM INTERFACE ${lcms2_SOURCE_DIR}/include)
target_link_libraries(pinned_lcms2 INTERFACE lcms2)
