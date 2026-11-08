# Common part of the static Linux toolchains: musl-cross-make compilers from the toolchain image
# (docker/toolchain.Dockerfile, /opt/cross). Included by <triple>.cmake after setting
# CMAKE_SYSTEM_PROCESSOR and UNI_TRIPLE.
#
# UNI_COMPILER=gcc (default) uses <triple>-gcc; UNI_COMPILER=clang uses clang + lld with the musl
# sysroot and libgcc/crt files of the same cross toolchain (for the size comparison).
set(CMAKE_SYSTEM_NAME Linux)
set(UNI_CROSS_ROOT "/opt/cross" CACHE PATH "musl-cross-make installation")
set(UNI_COMPILER "gcc" CACHE STRING "gcc or clang")
set(UNI_STATIC ON CACHE BOOL "fully static binary" FORCE)

if(UNI_COMPILER STREQUAL "clang")
  set(CMAKE_C_COMPILER clang)
  set(CMAKE_C_COMPILER_TARGET ${UNI_TRIPLE})
  set(CMAKE_SYSROOT ${UNI_CROSS_ROOT}/${UNI_TRIPLE})
  set(CMAKE_C_FLAGS_INIT "--gcc-toolchain=${UNI_CROSS_ROOT}")
  set(CMAKE_EXE_LINKER_FLAGS_INIT "--gcc-toolchain=${UNI_CROSS_ROOT} -fuse-ld=lld")
  set(CMAKE_AR llvm-ar)
  set(CMAKE_RANLIB llvm-ranlib)
else()
  set(CMAKE_C_COMPILER ${UNI_CROSS_ROOT}/bin/${UNI_TRIPLE}-gcc)
  set(CMAKE_AR ${UNI_CROSS_ROOT}/bin/${UNI_TRIPLE}-gcc-ar)
  set(CMAKE_RANLIB ${UNI_CROSS_ROOT}/bin/${UNI_TRIPLE}-gcc-ranlib)
endif()

set(CMAKE_FIND_ROOT_PATH ${UNI_CROSS_ROOT}/${UNI_TRIPLE})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# Run binaries of another architecture with qemu-user (tests, CMAKE_CROSSCOMPILING_EMULATOR).
cmake_host_system_information(RESULT _uni_host_arch QUERY OS_PLATFORM)
if(NOT _uni_host_arch STREQUAL CMAKE_SYSTEM_PROCESSOR)
  set(CMAKE_CROSSCOMPILING_EMULATOR qemu-${CMAKE_SYSTEM_PROCESSOR})
endif()
