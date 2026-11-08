# Generator selection by uni-name/uni-version (pkg/uni of the Go implementation).
add_library(uni_uni STATIC uni.c)
target_include_directories(uni_uni PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(uni_uni PUBLIC uni_pb)
