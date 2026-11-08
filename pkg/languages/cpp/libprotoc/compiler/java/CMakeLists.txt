# The shared core of compiler/java of protobuf v36.2 (libprotoc_java.h), used by the Java, Kotlin
# and gRPC Java generators. One translation unit (libprotoc_java.c); built only when a generator
# links it.
add_library(cpp_libprotoc_java STATIC EXCLUDE_FROM_ALL libprotoc_java.c)
target_include_directories(cpp_libprotoc_java PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(cpp_libprotoc_java PUBLIC cpp_libprotoc)
