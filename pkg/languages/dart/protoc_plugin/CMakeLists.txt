# protoc_plugin 25.1.0, the Dart code generator of protocolbuffers/dart (protoc_plugin.h).
add_library(dart_protoc_plugin STATIC protoc_plugin.c)
target_include_directories(dart_protoc_plugin PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dart_protoc_plugin PUBLIC dart_formatters_dart_style dart_protobuf dart_std dart_path uni_pb)
