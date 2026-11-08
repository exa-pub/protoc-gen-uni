# Port of GRPCCodeGen (grpc-swift-2 2.4.4): the StructuredSwift representation, its builders and
# TextBasedRenderer, used by grpc/swift-protobuf.
add_library(swift_grpc_codegen STATIC client.c renderer.c server.c service_metadata.c struct.c translator.c)
target_include_directories(swift_grpc_codegen PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(swift_grpc_codegen PUBLIC swift_std)

uni_add_unit_test(grpc_codegen_test SOURCES tests/grpc_codegen_test.c LIBRARIES swift_grpc_codegen)
