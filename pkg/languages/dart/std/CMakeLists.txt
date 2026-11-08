# The Dart standard library as far as it shows in generated code (dart_std.h).
add_library(dart_std STATIC dart_std.c)
target_include_directories(dart_std PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dart_std PUBLIC uni_core uni_num)
