FetchContent_Declare(libde265
  GIT_REPOSITORY  https://github.com/strukturag/libde265.git
  GIT_TAG         v1.1.3
  BINARY_DIR      libde265-build
  SUBBUILD_DIR    libde265-subbuild
)

option(ENABLE_SDL "" OFF)
# The dec265 command line tool.
option(ENABLE_DECODER "" OFF)

# libde265 picks its AVX2 and AVX-512 kernels at run time with
# __builtin_cpu_supports(), whose CPU table comes from the compiler's runtime
# library. zig's has none, so where the check does not link, those kernels are
# left out and the SSE ones run.
include(CheckCSourceCompiles)
check_c_source_compiles(
  "int main(void) { __builtin_cpu_init(); return __builtin_cpu_supports(\"avx2\"); }"
  LIBDE265_CPU_SUPPORTS_LINKS)
if(NOT LIBDE265_CPU_SUPPORTS_LINKS)
  set(ENABLE_AVX2 OFF)
  set(ENABLE_AVX512 OFF)
endif()

FetchContent_MakeAvailable(libde265)

set(LIBDE265_LIBRARIES de265)
set(LIBDE265_INCLUDE_DIRS
  "${libde265_SOURCE_DIR}"
  "${libde265_BINARY_DIR}"
)
