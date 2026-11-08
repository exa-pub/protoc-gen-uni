# The descriptor model shared by the plugin libraries (proto.h, README.md).
add_library(uni_proto STATIC desc.c features.c message.c names.c pool.c source.c wkt.c)
target_include_directories(uni_proto PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(uni_proto PUBLIC uni_pb)

uni_add_unit_test(proto_test SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/tests/proto_test.c LIBRARIES uni_proto)
