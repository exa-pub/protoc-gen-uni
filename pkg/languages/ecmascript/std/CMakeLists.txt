# The parts of the ECMAScript standard library that show in generated code (Number#toString).
add_library(uni_es_std STATIC number.c path.c value.c)
target_include_directories(uni_es_std PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(uni_es_std PUBLIC uni_num uni_core)
