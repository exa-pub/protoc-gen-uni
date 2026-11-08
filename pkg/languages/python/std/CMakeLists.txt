# Python language support (python.h): CPython 3.13 semantics of str (whitespace, strip,
# isprintable, sorted, splitlines, codec error texts), repr() of str/bytes/int/bool/float, a string-literal subset of
# ast.parse, int() and float() of str, sys.stdlib_module_names, hashlib.sha256, shlex, optparse and
# textwrap.wrap (CPython 3.11).
add_library(python_std STATIC unicode.c codecs.c sort.c repr.c literal.c lines.c stdlib_names.c hashlib.c number.c
  shlex.c optparse.c format.c textwrap.c)
target_include_directories(python_std PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(python_std PUBLIC uni_core)

uni_add_unit_test(python_test SOURCES tests/python_test.c LIBRARIES python_std)
uni_add_unit_test(python_stdlib_test SOURCES tests/stdlib_test.c LIBRARIES python_std)
uni_add_unit_test(python_textwrap_test SOURCES tests/textwrap_test.c LIBRARIES python_std)
uni_add_unit_test(python_cli_test SOURCES tests/cli_test.c LIBRARIES python_std)
