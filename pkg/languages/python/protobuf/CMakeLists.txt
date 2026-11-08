# python-protobuf (PyPI protobuf, the pure-Python implementation; python_protobuf.h): text_format.Merge
# for the plugins written in Python that parse text format options (community/nanopb).
add_library(python_protobuf STATIC text_format.c)
target_include_directories(python_protobuf PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(python_protobuf PUBLIC python_std uni_pb)

uni_add_unit_test(python_protobuf_test SOURCES tests/protobuf_test.c LIBRARIES python_protobuf)
