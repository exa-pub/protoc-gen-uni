# Go language support: the Go 1.26.5 standard library port (go/parser, go/printer, ...: the
# formatting compiler/protogen applies to generated code), the parts of the protobuf-go runtime that
# protogen relies on (protodesc: wire format with Go semantics, descriptors; features; tag; strs;
# genid) and compiler/protogen itself.

set(_go_core uni_core)

# One translation unit per Go version (unity build: everything static except the prefixed API).
set(_go1_26_5 ${CMAKE_CURRENT_SOURCE_DIR}/stdlib/go1.26.5)
add_library(go_stdlib_go1_26_5 STATIC ${_go1_26_5}/go1_26_5.c)
# Size/speed experiment (not the default): another optimization level for this translation unit
# only, e.g. -DUNI_GOFMT_OPT=Os (with -DUNI_OPT=O3: Os here gave 508 888 → 406 488 B, +6 % time).
set(UNI_GOFMT_OPT "" CACHE STRING "optimization level of the Go formatter TU only (O2, Os; empty: UNI_OPT)")
if(UNI_GOFMT_OPT)
  set_source_files_properties(${_go1_26_5}/go1_26_5.c PROPERTIES COMPILE_OPTIONS -${UNI_GOFMT_OPT})
endif()
target_include_directories(go_stdlib_go1_26_5 PUBLIC ${_go1_26_5})
target_link_libraries(go_stdlib_go1_26_5 PUBLIC ${_go_core})

# protobuf-go v1.36.12 pieces (internal/genid is header-only).
add_library(go_strs STATIC strs/strs.c)
target_include_directories(go_strs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/strs)
target_link_libraries(go_strs PUBLIC go_stdlib_go1_26_5)
add_library(go_features STATIC features/features.c)
target_include_directories(go_features PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/features ${CMAKE_CURRENT_SOURCE_DIR}/genid)
target_link_libraries(go_features PUBLIC uni_pb)
add_library(go_protodesc STATIC protodesc/wire.c protodesc/schema.c protodesc/desc.c protodesc/validate.c)
target_include_directories(go_protodesc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/protodesc)
target_link_libraries(go_protodesc PUBLIC go_features)
add_library(go_tag STATIC tag/tag.c)
target_include_directories(go_tag PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/tag)
target_link_libraries(go_tag PUBLIC go_protodesc go_stdlib_go1_26_5)
add_library(go_protogen STATIC protogen/protogen.c protogen/generated_file.c)
target_include_directories(go_protogen PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/protogen)
target_link_libraries(go_protogen PUBLIC go_protodesc go_strs go_stdlib_go1_26_5)

# Unit tests include go1_26_5.c themselves (to reach static functions), so they link only core.
foreach(_t scanner strconv tabwriter comment parser printer)
  if(EXISTS ${_go1_26_5}/tests/${_t}_test.c)
    uni_add_unit_test(go1_26_5_${_t}
      SOURCES ${_go1_26_5}/tests/${_t}_test.c
      LIBRARIES ${_go_core}
      INCLUDES ${_go1_26_5})
    # comment_test finds its testdata via __FILE__, relative to the source root.
    set_tests_properties(unit/go1_26_5_${_t} PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  endif()
endforeach()

# Corpus test: formats every case of build/gofmt-corpus/cases (dev-only, see
# stdlib/go1.26.5/testdata/make-corpus.sh) and compares byte for byte; skipped if absent.
set(UNI_GOFMT_CORPUS "${CMAKE_SOURCE_DIR}/build/gofmt-corpus/cases" CACHE PATH
  "corpus of the Go formatter port (make-corpus.sh)")
uni_add_unit_test(go1_26_5_corpus
  SOURCES ${_go1_26_5}/tests/corpus_test.c
  LIBRARIES go_stdlib_go1_26_5
  INCLUDES ${_go1_26_5})
set_tests_properties(unit/go1_26_5_corpus PROPERTIES
  SKIP_RETURN_CODE 77
  ENVIRONMENT "UNI_GOFMT_CORPUS=${UNI_GOFMT_CORPUS}")

# The same over Go 1.26.5's own sources (full Go syntax; make-corpus.sh step 3).
add_test(NAME unit/go1_26_5_corpus_gosrc COMMAND go1_26_5_corpus -k)
set_tests_properties(unit/go1_26_5_corpus_gosrc PROPERTIES
  LABELS unit
  SKIP_RETURN_CODE 77
  ENVIRONMENT "UNI_GOFMT_CORPUS=${UNI_GOFMT_CORPUS}/../gosrc")

# Robustness: mutated corpus inputs (truncations, byte flips) must not crash or hang.
add_test(NAME unit/go1_26_5_corpus_mutate COMMAND go1_26_5_corpus --mutate 20 1)
set_tests_properties(unit/go1_26_5_corpus_mutate PROPERTIES
  LABELS unit
  SKIP_RETURN_CODE 77
  TIMEOUT 600
  ENVIRONMENT "UNI_GOFMT_CORPUS=${UNI_GOFMT_CORPUS}")
