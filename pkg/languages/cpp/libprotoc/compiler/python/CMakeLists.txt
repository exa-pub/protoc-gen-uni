# The shared part of compiler/python of protobuf v36.2 (libprotoc_python.h), used by the Python
# and pyi generators. One translation unit (libprotoc_python.c); built only when a generator
# links it.
add_library(cpp_libprotoc_python STATIC EXCLUDE_FROM_ALL libprotoc_python.c)
target_include_directories(cpp_libprotoc_python PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(cpp_libprotoc_python PUBLIC cpp_libprotoc)
