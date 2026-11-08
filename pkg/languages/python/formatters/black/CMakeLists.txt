# black 24.3.0: the Python formatter of community/danielgtaylor-betterproto (README.md).
# Generators build its nodes (black.h); no tokenizer or parser.
set(_bk ${CMAKE_CURRENT_SOURCE_DIR})
add_library(python_formatters_black STATIC ${_bk}/black.c)
target_include_directories(python_formatters_black PUBLIC ${_bk})
target_link_libraries(python_formatters_black PUBLIC uni_core python_std)

# Node fixtures with the output of black 24.3.0 (Python 3.11) on the same source.
uni_add_unit_test(black_test SOURCES ${_bk}/tests/black_test.c LIBRARIES python_formatters_black)
