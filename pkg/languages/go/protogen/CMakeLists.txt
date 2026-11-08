# compiler/protogen of protobuf-go v1.31.0 where it differs from v1.36.12 (target go_protogen in
# the parent directory), for the plugins built with that version (community/planetscale-vtprotobuf):
# protogen_v1.31.0.c, public header protogen_v1.31.0.h.
# Defined here rather than next to go_protogen so that its unit test is switched off when no
# generator uses it (uni_register_unit_tests works per directory).
add_library(go_protogen_v1_31_0 STATIC protogen_v1.31.0.c)
target_include_directories(go_protogen_v1_31_0 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(go_protogen_v1_31_0 PUBLIC go_protogen)

uni_add_unit_test(go_protogen_v1_31_0_test
  SOURCES protogen_test_v1.31.0.c
  LIBRARIES go_protogen_v1_31_0)

# compiler/protogen of protobuf-go v1.36.7 where it differs from v1.36.12, for the plugins built
# with that version (community/google-gnostic-openapi): protogen_v1.36.7.c, protogen_v1.36.7.h.
add_library(go_protogen_v1_36_7 STATIC protogen_v1.36.7.c)
target_include_directories(go_protogen_v1_36_7 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(go_protogen_v1_36_7 PUBLIC go_protogen)
