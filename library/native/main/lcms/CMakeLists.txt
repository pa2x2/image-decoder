FetchContent_Declare(lcms2
  GIT_REPOSITORY  https://github.com/mm2/Little-CMS
  GIT_TAG         lcms2.16
  PATCH_COMMAND   git apply ${CMAKE_CURRENT_LIST_DIR}/lcms2.12.patch || true
  BINARY_DIR      build
  SUBBUILD_DIR    subbuild
)

FetchContent_MakeAvailable(lcms2)

add_library(pinned_lcms2 INTERFACE)
target_include_directories(pinned_lcms2 SYSTEM INTERFACE
  ${lcms2_BINARY_DIR}
  ${lcms2_SOURCE_DIR}
  ${lcms2_SOURCE_DIR}/include
)
target_link_libraries(pinned_lcms2 INTERFACE lcms2)
