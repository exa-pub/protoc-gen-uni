# Swift standard library behavior that shows in output: String/Character operations on grapheme
# clusters (split, dropFirst, lines).
add_library(swift_std STATIC string.c)
target_include_directories(swift_std PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(swift_std PUBLIC uni_core)

uni_add_unit_test(swift_std_test SOURCES tests/swift_std_test.c LIBRARIES swift_std)
