# Port of the Go github.com/bufbuild/protoplugin at commit 750e09ce93e1: request validation and the
# ResponseWriter (validation, normalization and marshaling of the CodeGeneratorResponse).
add_library(go_protoplugin STATIC validate.c response_writer.c)
target_include_directories(go_protoplugin PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(go_protoplugin PUBLIC uni_pb)

uni_add_unit_test(go_protoplugin_test
  SOURCES protoplugin_test.c
  LIBRARIES go_protoplugin go_protogen)
