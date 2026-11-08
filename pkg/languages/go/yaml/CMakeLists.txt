# go.yaml.in/yaml/v3 v3.0.4: yaml.Node marshalling (emitter) and unmarshalling (scanner, parser).
add_library(go_yaml STATIC encode.c emitterc.c resolve.c decode.c parserc.c scannerc.c readerc.c)
target_include_directories(go_yaml PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(go_yaml PUBLIC go_std)

# Differential test against go.yaml.in/yaml/v3 v3.0.4 (tests/testdata/README.md).
uni_add_unit_test(go_yaml_test
  SOURCES tests/yaml_test.c
  LIBRARIES go_yaml)
set_tests_properties(unit/go_yaml_test PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
