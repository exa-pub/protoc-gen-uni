# Port of the parts of @protobuf-ts/runtime 2.11.1 that the protobuf-ts plugin runs at generation
# time (reflection info, create, the reflection binary reader and JSON writer, enum objects).
add_library(uni_es_protobuf_ts_runtime STATIC runtime.c)
target_include_directories(uni_es_protobuf_ts_runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(uni_es_protobuf_ts_runtime PUBLIC uni_es_std uni_core)
