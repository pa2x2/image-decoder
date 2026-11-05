# Cross-compiles the desktop library for x86_64 Linux with zig, so it loads on any distribution with glibc 2.28 or
# later whatever machine builds it. Needs zig on the PATH.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER ${CMAKE_CURRENT_LIST_DIR}/zig-cc)
set(CMAKE_CXX_COMPILER ${CMAKE_CURRENT_LIST_DIR}/zig-c++)
set(CMAKE_ASM_COMPILER ${CMAKE_CURRENT_LIST_DIR}/zig-cc)
set(CMAKE_AR ${CMAKE_CURRENT_LIST_DIR}/zig-ar)
set(CMAKE_RANLIB ${CMAKE_CURRENT_LIST_DIR}/zig-ranlib)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
# Every library is built here from source; none may come from the building machine, so searches for libraries and
# packages look only under a root that holds nothing, and pkg-config sees no modules. Tools such as nasm and meson are
# still the machine's.
set(CMAKE_FIND_ROOT_PATH ${CMAKE_CURRENT_LIST_DIR}/no-system-libraries)
set(ENV{PKG_CONFIG_LIBDIR} ${CMAKE_CURRENT_LIST_DIR}/no-system-libraries)
set(ENV{PKG_CONFIG_PATH} "")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
