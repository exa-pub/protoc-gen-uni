# Port of SwiftProtobufPluginLibrary (swift-protobuf 1.39.0): descriptors with feature
# resolution, naming, CodePrinter, source comments and the plugin driver; the edition defaults of
# 1.37.0 (connectrpc/swift) and 1.38.1 (grpc/swift).
add_library(swift_swift_protobuf STATIC comments.c descriptor.c edition_defaults_1.37.0.c
  edition_defaults_1.38.1.c namer.c naming.c plugin.c printer.c)
target_include_directories(swift_swift_protobuf PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(swift_swift_protobuf PUBLIC uni_pb)

uni_add_unit_test(swift_protobuf_test SOURCES tests/swift_protobuf_test.c LIBRARIES swift_swift_protobuf)
