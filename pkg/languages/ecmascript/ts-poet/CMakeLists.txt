# ts-poet 6.12.0: imports, defs, conditional outputs and file assembly for generators that build
# dprint-typescript nodes (ts_poet.h).
add_library(ecmascript_ts_poet STATIC ts_poet.c)
target_include_directories(ecmascript_ts_poet PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ecmascript_ts_poet PUBLIC ecmascript_formatters_dprint_typescript uni_es_std uni_core)

uni_add_unit_test(ts_poet
  SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/tests/ts_poet_test.c
  LIBRARIES ecmascript_ts_poet ecmascript_case_anything)
