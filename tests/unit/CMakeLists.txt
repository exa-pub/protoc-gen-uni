# Unit tests (uni_add_unit_test, see the root CMakeLists.txt).
uni_add_unit_test(core_test SOURCES core_test.c LIBRARIES uni_core)
uni_add_unit_test(pb_test SOURCES pb_test.c LIBRARIES uni_pb)
uni_add_unit_test(uni_test SOURCES uni_test.c LIBRARIES uni_uni)
uni_add_unit_test(protoplugin_test SOURCES protoplugin_test.c LIBRARIES uni_es_protoplugin
  INCLUDES ${CMAKE_SOURCE_DIR}/pkg/languages/ecmascript/protoplugin)
uni_add_unit_test(go_test SOURCES go_test.c LIBRARIES go_protogen go_tag)
