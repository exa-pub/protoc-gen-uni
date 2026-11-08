# google.golang.org/genproto/googleapis/api/annotations: the google.api extensions and HttpRule.
add_library(go_genproto STATIC annotations.c)
target_include_directories(go_genproto PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(go_genproto PUBLIC go_protodesc)
