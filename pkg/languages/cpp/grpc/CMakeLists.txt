# grpc_plugin_support of grpc v1.85.1 (grpc.h): the helpers of src/compiler shared by the grpc
# protoc plugins written in C++ (grpc/{cpp,csharp,objc,php,python,ruby}), over libprotoc. The
# generator links it with the libprotoc version of its protobuf (cpp_libprotoc_35.1 for v1.85.1).
# One translation unit (grpc.c).
add_library(cpp_grpc STATIC grpc.c)
target_include_directories(cpp_grpc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(cpp_grpc PUBLIC cpp_libprotoc)

uni_add_unit_test(grpc_test
  SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/tests/grpc_test.c
  LIBRARIES cpp_grpc)

# The parts of the helpers of grpc v1.25.0 that differ (generator_helpers_1.25.0.c), for grpc/node
# (grpc-tools vendors them).
add_library(cpp_grpc_1.25.0 STATIC generator_helpers_1.25.0.c)
target_link_libraries(cpp_grpc_1.25.0 PUBLIC cpp_grpc)
