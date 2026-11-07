set(DAV1D_VERSION 1.5.4)

# Meson builds dav1d below only when its library is missing, so each version
# builds in a directory of its own and a new pin never links an old build.
FetchContent_Declare(dav1d
  GIT_REPOSITORY    https://github.com/videolan/dav1d.git
  GIT_TAG           ${DAV1D_VERSION}
  CONFIGURE_COMMAND ""
  BUILD_COMMAND     ""
  BINARY_DIR        dav1d-build-${DAV1D_VERSION}
  SUBBUILD_DIR      dav1d-subbuild
)
FetchContent_MakeAvailable(dav1d)

set(DAV1D_FILENAME "${dav1d_BINARY_DIR}/src/libdav1d.a")

if(NOT EXISTS "${DAV1D_FILENAME}")
  if(ANDROID)
    set(ENV{ANDROID_NDK} ${CMAKE_ANDROID_NDK})
    set(PREPARE_DAV1D "${CMAKE_CURRENT_LIST_DIR}/generate_dav1d_android_cross_compile.sh -a ${CMAKE_ANDROID_ARCH}")
    set(DAV1D_CROSS_FILE android_cross.txt)
  else()
    # Meson builds dav1d with the compilers of this CMake build, so the
    # desktop library's dav1d targets the same glibc as the rest of it.
    set(PREPARE_DAV1D "true")
    set(DAV1D_CROSS_FILE desktop_cross.txt)
    file(WRITE "${dav1d_BINARY_DIR}/${DAV1D_CROSS_FILE}"
      "[binaries]\n"
      "c = '${CMAKE_C_COMPILER}'\n"
      "cpp = '${CMAKE_CXX_COMPILER}'\n"
      "ar = '${CMAKE_AR}'\n"
      "strip = 'strip'\n"
      "[built-in options]\n"
      "b_staticpic = true\n"
      "[host_machine]\n"
      "system = 'linux'\n"
      "cpu_family = '${CMAKE_SYSTEM_PROCESSOR}'\n"
      "cpu = '${CMAKE_SYSTEM_PROCESSOR}'\n"
      "endian = 'little'\n"
    )
  endif()
  set(CONFIG_DAV1D "meson setup --buildtype release --default-library static --cross-file ${DAV1D_CROSS_FILE} -Denable_tools=false -Denable_tests=false ${dav1d_SOURCE_DIR}")
  set(BUILD_DAV1D "ninja")

  execute_process(
    COMMAND bash -c "${PREPARE_DAV1D} && ${CONFIG_DAV1D} && ${BUILD_DAV1D}"
    WORKING_DIRECTORY ${dav1d_BINARY_DIR}
  )
endif()

if(NOT EXISTS "${DAV1D_FILENAME}")
  message(FATAL_ERROR "libavif: ${DAV1D_FILENAME} is missing")
endif()

set(DAV1D_LIBRARIES "${DAV1D_FILENAME}")
set(DAV1D_INCLUDE_DIRS
  "${dav1d_BINARY_DIR}"
  "${dav1d_BINARY_DIR}/include"
  "${dav1d_BINARY_DIR}/include/dav1d"
  "${dav1d_SOURCE_DIR}/include"
)
