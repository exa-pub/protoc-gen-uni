# protobuf-py v0.7.0 (protobuf_py.h): the descriptor model, the binary wire format with
# protobuf-py's semantics and the protoc plugin framework protobuf.plugin, for the plugins built on
# it (bufbuild/py, connectrpc/py, bufbuild/grpc-py).
add_library(python_protobuf_py STATIC schema.c wire.c descriptors.c text_format.c sanitization.c
  plugin/ident.c plugin/file.c plugin/source_code_info.c plugin/map_imports.c plugin/options.c
  plugin/run.c)
target_include_directories(python_protobuf_py PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(python_protobuf_py PUBLIC python_std uni_pb)

uni_add_unit_test(protobuf_py_test SOURCES tests/protobuf_py_test.c LIBRARIES python_protobuf_py)
