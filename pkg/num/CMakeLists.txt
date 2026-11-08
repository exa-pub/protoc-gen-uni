# Shortest round-trip digits (num.h), the numeric algorithm under the number printing of the
# language runtimes (pkg/languages/<language>/std).
add_library(uni_num STATIC shortest.c)
target_include_directories(uni_num PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(uni_num PUBLIC uni_core)

uni_add_unit_test(num_test SOURCES num_test.c LIBRARIES uni_num)
