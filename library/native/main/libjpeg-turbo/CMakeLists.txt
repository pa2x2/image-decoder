include(ExternalProject)

# libjpeg-turbo refuses to be added with add_subdirectory(), so FetchContent
# only downloads it (SOURCE_SUBDIR names a directory without a build) and
# ExternalProject builds it with this build's toolchain and flags.
FetchContent_Declare(libjpeg-turbo
  GIT_REPOSITORY  https://github.com/libjpeg-turbo/libjpeg-turbo
  GIT_TAG         3.2.1
  SOURCE_SUBDIR   built-by-external-project
  SUBBUILD_DIR    subbuild
)
FetchContent_MakeAvailable(libjpeg-turbo)

set(libjpeg_turbo_build_dir ${CMAKE_CURRENT_BINARY_DIR}/build)
set(libjpeg_turbo_library ${libjpeg_turbo_build_dir}/${CMAKE_STATIC_LIBRARY_PREFIX}jpeg${CMAKE_STATIC_LIBRARY_SUFFIX})

set(libjpeg_turbo_arguments
  -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
  -DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON
  "-DCMAKE_C_FLAGS=${CMAKE_C_FLAGS}"
  -DENABLE_SHARED=0
  -DENABLE_STATIC=1
  -DWITH_JPEG8=1
  -DWITH_TURBOJPEG=0
  -DWITH_TOOLS=0
  -DWITH_TESTS=0
  -DREQUIRE_SIMD=1
)
if(CMAKE_TOOLCHAIN_FILE)
  list(APPEND libjpeg_turbo_arguments -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE})
else()
  list(APPEND libjpeg_turbo_arguments -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER})
endif()
if(ANDROID)
  list(APPEND libjpeg_turbo_arguments
    -DANDROID_ABI=${ANDROID_ABI}
    -DANDROID_PLATFORM=${ANDROID_PLATFORM}
  )
endif()

# The build step runs every time so that a new pin rebuilds what changed; with
# nothing changed it is a no-op.
ExternalProject_Add(libjpeg-turbo-build
  SOURCE_DIR        ${libjpeg-turbo_SOURCE_DIR}
  BINARY_DIR        ${libjpeg_turbo_build_dir}
  CMAKE_ARGS        ${libjpeg_turbo_arguments}
  BUILD_ALWAYS      TRUE
  BUILD_BYPRODUCTS  ${libjpeg_turbo_library}
  INSTALL_COMMAND   ""
)

# jconfig.h is generated into the build directory when the build runs; the
# directory has to exist before then for the imported target to name it.
file(MAKE_DIRECTORY ${libjpeg_turbo_build_dir})
add_library(libjpeg_turbo_static STATIC IMPORTED GLOBAL)
set_target_properties(libjpeg_turbo_static PROPERTIES IMPORTED_LOCATION ${libjpeg_turbo_library})
add_dependencies(libjpeg_turbo_static libjpeg-turbo-build)

add_library(pinned_libjpeg_turbo INTERFACE)
target_include_directories(pinned_libjpeg_turbo SYSTEM INTERFACE ${libjpeg_turbo_build_dir} ${libjpeg-turbo_SOURCE_DIR}/src)
target_link_libraries(pinned_libjpeg_turbo INTERFACE libjpeg_turbo_static)
target_compile_definitions(pinned_libjpeg_turbo INTERFACE HAVE_LIBJPEG)
