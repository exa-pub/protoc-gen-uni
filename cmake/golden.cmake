# E2E golden tests (tests/e2e/golden.sh): one ctest test per generator version and test input,
# named golden/<owner>/<plugin>/<version>/<test>, from the inputs of
# testdata/checksums/<owner>/<plugin>/<version>.buf.gen.yaml. Generators that the binary does not
# contain are skipped. buf is downloaded once by the setup/buf fixture unless UNI_BUF is set.
set(UNI_BUF "" CACHE FILEPATH "buf binary for the e2e tests (default: scripts/get-buf.sh)")
find_program(UNI_BASH bash REQUIRED)

if(UNI_BUF)
  set(_uni_buf ${UNI_BUF})
else()
  set(_uni_buf ${CMAKE_BINARY_DIR}/tools/buf)
  add_test(NAME setup/buf COMMAND ${CMAKE_COMMAND} -E env ${UNI_BASH} -c
    "ln -sf \"$(${CMAKE_SOURCE_DIR}/scripts/get-buf.sh ${CMAKE_BINARY_DIR}/tools)\" ${_uni_buf}")
  set_tests_properties(setup/buf PROPERTIES FIXTURES_SETUP buf)
endif()

set(_uni_runner)
if(CMAKE_CROSSCOMPILING_EMULATOR)
  list(JOIN CMAKE_CROSSCOMPILING_EMULATOR " " _uni_emulator)
  set(_uni_runner --runner "${_uni_emulator}")
endif()

file(GLOB _uni_templates ${CMAKE_SOURCE_DIR}/testdata/checksums/*/*/v*.buf.gen.yaml)
foreach(template IN LISTS _uni_templates)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${template})
  file(RELATIVE_PATH rel ${CMAKE_SOURCE_DIR}/testdata/checksums ${template})
  string(REGEX REPLACE "^(.*)/(v[^/]*)\\.buf\\.gen\\.yaml$" "\\1" name ${rel})
  string(REGEX REPLACE "^(.*)/(v[^/]*)\\.buf\\.gen\\.yaml$" "\\2" version ${rel})
  file(STRINGS ${template} inputs REGEX "^[ \t]*-[ \t]*directory:[ \t]*testdata/proto/")
  foreach(line IN LISTS inputs)
    string(REGEX REPLACE "^.*testdata/proto/([A-Za-z0-9._-]+).*$" "\\1" test "${line}")
    set(id golden/${name}/${version}/${test})
    add_test(NAME ${id}
      COMMAND ${UNI_BASH} ${CMAKE_SOURCE_DIR}/tests/e2e/golden.sh ${_uni_runner}
              $<TARGET_FILE:protoc-gen-uni> ${name} ${version} ${test})
    set_tests_properties(${id} PROPERTIES SKIP_RETURN_CODE 77 ENVIRONMENT "BUF=${_uni_buf}"
      FIXTURES_REQUIRED buf LABELS golden)
  endforeach()
endforeach()

# generators/, README files and testdata/checksums/ must match the generators of the binary.
add_test(NAME registry COMMAND ${UNI_BASH} ${CMAKE_SOURCE_DIR}/tests/e2e/check-registry.sh ${_uni_runner}
  $<TARGET_FILE:protoc-gen-uni>)
