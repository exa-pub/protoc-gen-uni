# Protobuf wire format and the schema tables of descriptor.proto and plugin.proto.
add_library(uni_pb STATIC message.c wire.c schema/descriptor.c schema/plugin.c)
target_include_directories(uni_pb PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(uni_pb PUBLIC uni_core)
