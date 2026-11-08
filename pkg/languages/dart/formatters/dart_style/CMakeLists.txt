# The dart_style 3.1.13 port (the formatter protocolbuffers/dart applies to generated code; short
# style only, no front end: the generator builds the nodes, see README.md).

set(_ds ${CMAKE_CURRENT_SOURCE_DIR})
add_library(dart_formatters_dart_style STATIC ${_ds}/dart_style.c)
target_include_directories(dart_formatters_dart_style PUBLIC ${_ds})
target_link_libraries(dart_formatters_dart_style PUBLIC uni_core)

# Node fixtures with the output of dart_style 3.1.13 (Dart SDK 3.13.3).
uni_add_unit_test(dart_style_format
  SOURCES ${_ds}/tests/format_test.c
  LIBRARIES dart_formatters_dart_style)
