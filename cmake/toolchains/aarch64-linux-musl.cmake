# Static Linux aarch64 binary (musl). See musl.cmake.
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(UNI_TRIPLE aarch64-linux-musl)
include(${CMAKE_CURRENT_LIST_DIR}/musl.cmake)
