# The Dart protobuf runtime as far as the output of protoc_plugin and connect-dart depends on it
# (dart_protobuf.h).
add_library(dart_protobuf STATIC schema.c coded_buffer.c coded_buffer_reader.c field_set.c json.c)
target_include_directories(dart_protobuf PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dart_protobuf PUBLIC uni_pb dart_std)

uni_add_unit_test(dart_protobuf_test SOURCES tests/dart_protobuf_test.c LIBRARIES dart_protobuf)
