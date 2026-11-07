FetchContent_Declare(libheif
  GIT_REPOSITORY  https://github.com/strukturag/libheif
  GIT_TAG         v1.23.7
  BINARY_DIR      build
  SUBBUILD_DIR    subbuild
)

# libheif looks its codecs up with find_package(), which reads
# CMAKE_FIND_PACKAGE_REDIRECTS_DIR before anywhere else; the config files
# written there point it at the codecs built here.
function(provide_heif_codec package include_dirs libraries)
  string(TOLOWER ${package} file_name)
  file(WRITE ${CMAKE_FIND_PACKAGE_REDIRECTS_DIR}/${file_name}-config.cmake
    "set(${package}_INCLUDE_DIRS \"${include_dirs}\")\n"
    "set(${package}_LIBRARIES \"${libraries}\")\n"
  )
endfunction()

if(WITH_HEIF)
  include(libde265.cmake)
  provide_heif_codec(LIBDE265 "${LIBDE265_INCLUDE_DIRS}" "${LIBDE265_LIBRARIES}")
endif()

if(WITH_AVIF)
  include(dav1d.cmake)
  provide_heif_codec(DAV1D "${DAV1D_INCLUDE_DIRS}" "${DAV1D_LIBRARIES}")
endif()

# The codecs are compiled into libheif, never loaded as plugins.
set(ENABLE_PLUGIN_LOADING OFF)
set(WITH_LIBDE265 ${WITH_HEIF})
set(WITH_DAV1D ${WITH_AVIF})
option(WITH_X265 "" OFF)
option(WITH_X264 "" OFF)
option(WITH_OpenH264_DECODER "" OFF)
option(WITH_AOM_DECODER "" OFF)
option(WITH_AOM_ENCODER "" OFF)
option(WITH_LIBSHARPYUV "" OFF)
option(WITH_EXAMPLES "" OFF)
option(WITH_GDK_PIXBUF "" OFF)
option(BUILD_TESTING "" OFF)
option(BUILD_DOCUMENTATION "" OFF)

FetchContent_MakeAvailable(libheif)

add_library(pinned_libheif INTERFACE)
target_include_directories(pinned_libheif SYSTEM INTERFACE ${libheif_BINARY_DIR} ${libheif_SOURCE_DIR}/libheif/api)
target_link_libraries(pinned_libheif INTERFACE heif)
target_compile_definitions(pinned_libheif INTERFACE HAVE_LIBHEIF)
