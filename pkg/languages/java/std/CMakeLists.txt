# Java language support (java_std.h): the layouts of Java Double.toString/Float.toString over the
# shortest round-trip digits of pkg/num; String decoding and compareTo, sorting, exceptions (string.c).
add_library(java_std STATIC double_to_decimal.c string.c)
target_include_directories(java_std PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(java_std PUBLIC uni_num)

uni_add_unit_test(java_std_test SOURCES tests/java_std_test.c LIBRARIES java_std)
