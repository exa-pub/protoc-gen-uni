# protoc-bridge v0.9.10, module protoc-gen (protocgen.h): CodeGenApp, CodeGenRequest and
# CodeGenResponse, the request handling of the JVM protoc plugins built with it (ScalaPB plugins).
add_library(scala_protoc_bridge STATIC protocgen.c)
target_include_directories(scala_protoc_bridge PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(scala_protoc_bridge PUBLIC java_protobuf_java uni_pb)
