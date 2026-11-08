# stringcase 1.2.0: the case conversions of betterproto (stringcase.h).
add_library(python_stringcase STATIC ${CMAKE_CURRENT_SOURCE_DIR}/stringcase.c)
target_include_directories(python_stringcase PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(python_stringcase PUBLIC uni_core)
