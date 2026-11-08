# package:path (url style, relative paths) and Uri.file as protoc_plugin uses them, and the posix
# Context of connect-dart (dart_path.h).
add_library(dart_path STATIC dart_path.c context.c)
target_include_directories(dart_path PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dart_path PUBLIC uni_core)

uni_add_unit_test(dart_path_test SOURCES tests/dart_path_test.c LIBRARIES dart_path)
