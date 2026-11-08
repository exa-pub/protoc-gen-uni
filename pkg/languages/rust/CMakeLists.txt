# Rust language support: the Rust standard library behavior that shows in generated code
# (Rust 1.91.1), heck 0.5.0 case conversions, the shared core of the protoc-gen-prost plugins
# (protoc-gen-prost 0.5.0 with the prost-build 0.14.1 pieces it shares), pbjson-build 0.8.0, the
# proc-macro2 1.0.106 literals and lexer, the syn 2.0.109 parsing of option text, tonic-build
# 0.14.2 and the prettyplease 0.2.37 formatter.
add_library(rust_std STATIC std/rust_std.c)
target_include_directories(rust_std PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/std)
target_link_libraries(rust_std PUBLIC uni_core uni_num)

add_library(rust_heck STATIC heck/heck.c)
target_include_directories(rust_heck PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/heck)
target_link_libraries(rust_heck PUBLIC uni_core)

add_library(rust_protoc_gen_prost STATIC
  protoc-gen-prost/ast.c protoc-gen-prost/decode.c protoc-gen-prost/ident.c protoc-gen-prost/module.c
  protoc-gen-prost/params.c protoc-gen-prost/request.c protoc-gen-prost/response.c)
target_include_directories(rust_protoc_gen_prost PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/protoc-gen-prost)
target_link_libraries(rust_protoc_gen_prost PUBLIC rust_heck rust_std uni_pb)

# pbjson-build 0.8.0 (influxdata/pbjson): unity build, README.md.
add_library(rust_pbjson_build STATIC pbjson-build/pbjson_build.c)
target_include_directories(rust_pbjson_build PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/pbjson-build)
target_link_libraries(rust_pbjson_build PUBLIC rust_heck uni_pb)
uni_add_unit_test(pbjson_build_test SOURCES pbjson-build/tests/pbjson_build_test.c LIBRARIES rust_pbjson_build)

add_library(rust_proc_macro2 STATIC proc-macro2/literal.c proc-macro2/parse.c)
target_include_directories(rust_proc_macro2 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/proc-macro2)
target_link_libraries(rust_proc_macro2 PUBLIC rust_std rust_formatters_prettyplease)

add_subdirectory(formatters/prettyplease)

# syn 2.0.109: the parsing of Rust text from plugin options into prettyplease nodes (syn/syn.h).
add_library(rust_syn STATIC syn/syn.c)
target_include_directories(rust_syn PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/syn)
target_link_libraries(rust_syn PUBLIC rust_proc_macro2 rust_formatters_prettyplease)

# tonic-build 0.14.2 (community/neoeinstein-tonic): unity build, README.md.
add_library(rust_tonic_build STATIC tonic-build/tonic_build.c)
target_include_directories(rust_tonic_build PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/tonic-build)
target_link_libraries(rust_tonic_build PUBLIC rust_syn rust_proc_macro2 rust_std)
uni_add_unit_test(tonic_build_test SOURCES tonic-build/tests/tonic_build_test.c LIBRARIES rust_tonic_build)

# buffa-codegen 0.9.2 (anthropics/buffa, connectrpc/rust): unity build, README.md.
add_library(rust_buffa_codegen STATIC buffa-codegen/buffa_codegen.c)
target_include_directories(rust_buffa_codegen PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/buffa-codegen)
target_link_libraries(rust_buffa_codegen PUBLIC rust_formatters_prettyplease rust_proc_macro2 rust_std uni_proto uni_pb)

uni_add_unit_test(rust_test SOURCES tests/rust_test.c LIBRARIES rust_protoc_gen_prost rust_proc_macro2)
