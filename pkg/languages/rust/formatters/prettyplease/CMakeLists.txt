# prettyplease 0.2.37: the Rust pretty printer of buffa-codegen, tonic-build and connectrpc-codegen
# (README.md). Generators build its nodes (prettyplease.h); no parser.
set(_pp ${CMAKE_CURRENT_SOURCE_DIR})
add_library(rust_formatters_prettyplease STATIC ${_pp}/prettyplease.c)
target_include_directories(rust_formatters_prettyplease PUBLIC ${_pp})
target_link_libraries(rust_formatters_prettyplease PUBLIC uni_core)

# Node fixtures with the output of prettyplease 0.2.37 (syn 2.0.114) on the same source.
uni_add_unit_test(prettyplease_test SOURCES ${_pp}/tests/prettyplease_test.c
  LIBRARIES rust_formatters_prettyplease)
