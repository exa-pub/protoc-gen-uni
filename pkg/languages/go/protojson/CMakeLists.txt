# google.golang.org/protobuf encoding/protojson: the JSON of google.protobuf.Value
# (grpc-ecosystem/openapiv2).
add_library(go_protojson STATIC protojson.c)
target_include_directories(go_protojson PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(go_protojson PUBLIC go_protodesc go_std)
