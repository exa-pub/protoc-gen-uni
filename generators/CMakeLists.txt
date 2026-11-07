# All generators and the registry. Every version directory is a unity build: only its
# generator.c is compiled, it includes the other .c files of the directory.
add_library(uni_generators STATIC
  registry.c
  bufbuild/es/v2.17.0/generator.c
  protocolbuffers/go/v1.36.12/generator.c
)
target_include_directories(uni_generators PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(uni_generators PUBLIC uni_uni uni_es_protoplugin go_protogen go_tag)
