cmake_minimum_required(VERSION 3.25)
project(protoc-gen-uni LANGUAGES C)

# Static Linux builds set UNI_STATIC in their toolchain file (cmake/toolchains/musl.cmake).
option(UNI_STATIC "fully static binary" OFF)
option(UNI_FUZZ "libFuzzer harness fuzz/fuzz_request.c (clang; use with UNI_SANITIZE)" OFF)
include(cmake/flags.cmake)
if(UNI_FUZZ)
  # coverage instrumentation for everything; the harness links libFuzzer itself
  add_compile_options(-fsanitize=fuzzer-no-link)
endif()

enable_testing()

# Unit tests: one executable per test file (tests/unit/test.h), run by ctest.
# Built only for the host or under an emulator; uses the same flags as the binary.
function(uni_add_unit_test name)
  cmake_parse_arguments(T "" "" "SOURCES;LIBRARIES;INCLUDES" ${ARGN})
  add_executable(${name} ${T_SOURCES})
  target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/tests/unit ${T_INCLUDES})
  target_link_libraries(${name} PRIVATE ${T_LIBRARIES})
  add_test(NAME unit/${name} COMMAND ${name})
  set_tests_properties(unit/${name} PROPERTIES LABELS unit)
endfunction()

# Libraries: every top-most CMakeLists.txt under pkg/ (pkg/core: util.h, sys.h; pkg/pb: wire
# format, schemas; pkg/uni: generator selection; pkg/languages/<language>/...: shared by the
# generators of one language; ...), then generators/ (discovered generators and the registry).
# Build a single target (e.g. --target protoc-gen-uni) to compile only what it needs.
file(GLOB_RECURSE _uni_pkg_lists ${CMAKE_SOURCE_DIR}/pkg/*/CMakeLists.txt)
list(SORT _uni_pkg_lists)
set(_uni_pkg_dirs)
foreach(f IN LISTS _uni_pkg_lists)
  get_filename_component(dir ${f} DIRECTORY)
  set(nested FALSE)
  foreach(d IN LISTS _uni_pkg_dirs)
    string(FIND "${dir}/" "${d}/" pos)
    if(pos EQUAL 0)
      set(nested TRUE)
    endif()
  endforeach()
  if(NOT nested)
    list(APPEND _uni_pkg_dirs ${dir})
    add_subdirectory(${dir})
  endif()
endforeach()
add_subdirectory(generators)

add_executable(protoc-gen-uni main.c)
target_link_libraries(protoc-gen-uni PRIVATE uni_generators)

add_subdirectory(tests/unit)

if(UNI_FUZZ)
  add_subdirectory(fuzz)
endif()

include(cmake/golden.cmake)
