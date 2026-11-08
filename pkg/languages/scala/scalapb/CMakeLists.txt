# ScalaPB 0.11.21 compiler-plugin (scalapb.h): the code generator library of ScalaPB, used by
# protoc-gen-scala (community/scalapb-scala) and the ScalaPB plugins of other projects
# (community/scalapb-zio-grpc). One translation unit (scalapb.c).
add_library(scala_scalapb STATIC scalapb.c)
target_include_directories(scala_scalapb PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(scala_scalapb PUBLIC scala_protoc_bridge java_protobuf_java java_std uni_pb)
