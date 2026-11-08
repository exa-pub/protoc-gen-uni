# Port of @bufbuild/protobuf (descriptor model, registry, names, embedding) for ECMAScript generators.
add_library(uni_es_protobuf STATIC descriptors.c embed.c features.c from_binary.c names.c nested.c registry.c
  symbols.c)
target_include_directories(uni_es_protobuf PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(uni_es_protobuf PUBLIC uni_pb)
