# Static Linux x86_64 binary (musl). See musl.cmake.
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(UNI_TRIPLE x86_64-linux-musl)
include(${CMAKE_CURRENT_LIST_DIR}/musl.cmake)
