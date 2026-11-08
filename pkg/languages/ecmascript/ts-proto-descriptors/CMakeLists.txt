# ts-proto-descriptors 2.1.0: CodeGeneratorRequest and descriptors as ts-proto sees them (tpd.h).
add_library(ecmascript_ts_proto_descriptors STATIC tpd.c)
target_include_directories(ecmascript_ts_proto_descriptors PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ecmascript_ts_proto_descriptors PUBLIC uni_pb uni_core)
