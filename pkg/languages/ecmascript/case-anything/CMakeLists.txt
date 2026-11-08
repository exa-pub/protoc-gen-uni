# case-anything 2.1.13 (camelCase), as used by ts-proto.
add_library(ecmascript_case_anything STATIC case_anything.c)
target_include_directories(ecmascript_case_anything PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ecmascript_case_anything PUBLIC uni_core)
