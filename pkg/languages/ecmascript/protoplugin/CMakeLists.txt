# Port of @bufbuild/protoplugin, the framework of ECMAScript generator plugins.
add_library(uni_es_protoplugin STATIC
  collate.c file_preamble.c generated_file.c import_path.c jsdoc.c names.c parameter.c plugin.c
  runtime_imports.c safe_identifier.c schema.c source_code_info.c)
target_include_directories(uni_es_protoplugin PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(uni_es_protoplugin PUBLIC uni_es_protobuf)
