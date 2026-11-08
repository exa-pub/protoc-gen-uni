# dprint-plugin-typescript 0.87.1 (+ dprint-core 0.62.1): the formatter ts-poet 6.12.0 runs on every
# file ts-proto generates, with a node-building API instead of the TypeScript parser (README.md).
add_library(ecmascript_formatters_dprint_typescript STATIC dprint_typescript.c)
target_include_directories(ecmascript_formatters_dprint_typescript PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ecmascript_formatters_dprint_typescript PUBLIC uni_core)

# Unit tests include the unity build to reach static functions.
uni_add_unit_test(dprint_typescript
  SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/tests/dprint_test.c
  LIBRARIES uni_core
  INCLUDES ${CMAKE_CURRENT_SOURCE_DIR})
