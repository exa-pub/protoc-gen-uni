# libprotoc 36.2 (libprotoc.h): the parts of protobuf's C++ library v36.2 used by the protoc
# plugins built from it (protocolbuffers/<lang> v36.2). One translation unit (libprotoc.c).
add_library(cpp_libprotoc STATIC libprotoc.c)
target_include_directories(cpp_libprotoc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(cpp_libprotoc PUBLIC uni_proto)

# Other protobuf versions (LpVersion, libprotoc.h): what each does differently from 36.2, linked
# only by the generators built with it.
add_library(cpp_libprotoc_35.1 STATIC libprotoc_35.1.c)
target_link_libraries(cpp_libprotoc_35.1 PUBLIC cpp_libprotoc)
add_library(cpp_libprotoc_33.4 STATIC libprotoc_33.4.c)
target_link_libraries(cpp_libprotoc_33.4 PUBLIC cpp_libprotoc)
# 29.1: its difference from 33.4 (its base).
add_library(cpp_libprotoc_29.1 STATIC libprotoc_29.1.c)
target_link_libraries(cpp_libprotoc_29.1 PUBLIC cpp_libprotoc_33.4)
# 3.21.12 (the 3.x line: no editions, the old io::Printer of printer_3.21.12.h).
add_library(cpp_libprotoc_3.21.12 STATIC libprotoc_3.21.12.c printer_3.21.12.c)
target_link_libraries(cpp_libprotoc_3.21.12 PUBLIC cpp_libprotoc)
# 3.19.1: its difference from 3.21.12 (its base; the same io::Printer).
add_library(cpp_libprotoc_3.19.1 STATIC libprotoc_3.19.1.c)
target_link_libraries(cpp_libprotoc_3.19.1 PUBLIC cpp_libprotoc_3.21.12)

# compiler/java: the shared core of the Java generators (libprotoc_java.h).
add_subdirectory(compiler/java)

# compiler/python: the part shared by the Python and pyi generators (libprotoc_python.h).
add_subdirectory(compiler/python)

# The unit test of the 33.4 version, the SCC analyzer and Annotate (includes both .c files).
uni_add_unit_test(libprotoc_33.4_test
  SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/tests/libprotoc_33.4_test.c
  LIBRARIES uni_proto
  INCLUDES ${CMAKE_CURRENT_SOURCE_DIR})

# The unit test of the 35.1 version (includes both .c files).
uni_add_unit_test(libprotoc_35.1_test
  SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/tests/libprotoc_35.1_test.c
  LIBRARIES uni_proto
  INCLUDES ${CMAKE_CURRENT_SOURCE_DIR})

# The unit test of the 29.1 version (includes libprotoc.c, its base 33.4 and 29.1).
uni_add_unit_test(libprotoc_29.1_test
  SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/tests/libprotoc_29.1_test.c
  LIBRARIES uni_proto
  INCLUDES ${CMAKE_CURRENT_SOURCE_DIR})

# The unit test includes libprotoc.c itself (to reach static functions).
uni_add_unit_test(libprotoc_test
  SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/tests/libprotoc_test.c
  LIBRARIES uni_proto
  INCLUDES ${CMAKE_CURRENT_SOURCE_DIR})
