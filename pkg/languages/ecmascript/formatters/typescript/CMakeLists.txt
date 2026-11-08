# TypeScript 3.9.10 printer subset (createPrinter, the node factory, parsed nodes from a compact
# encoding), used by community/timostamm-protobuf-ts. One translation unit (typescript.c includes
# the other .c files); only the ts_* API of typescript.h is exported.
set(_ts ${CMAKE_CURRENT_SOURCE_DIR})
add_library(ecmascript_formatters_typescript STATIC ${_ts}/typescript.c)
target_include_directories(ecmascript_formatters_typescript PUBLIC ${_ts})
target_link_libraries(ecmascript_formatters_typescript PUBLIC uni_core)

# Unit tests (the test includes typescript.c itself, to reach static functions).
uni_add_unit_test(typescript_printer
  SOURCES ${_ts}/tests/printer_test.c
  LIBRARIES uni_core
  INCLUDES ${_ts} ${_ts}/tests)
