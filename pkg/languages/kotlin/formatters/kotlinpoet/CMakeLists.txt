# KotlinPoet 2.4.0 and 2.2.0: the Kotlin code builder of the JVM plugins that emit Kotlin (kotlinpoet.h,
# README.md). Generators build its specs; no Kotlin parser.
set(_kp ${CMAKE_CURRENT_SOURCE_DIR})
add_library(kotlin_formatters_kotlinpoet STATIC ${_kp}/code_block.c ${_kp}/code_writer.c ${_kp}/specs.c ${_kp}/util.c)
target_include_directories(kotlin_formatters_kotlinpoet PUBLIC ${_kp})
target_link_libraries(kotlin_formatters_kotlinpoet PUBLIC java_std uni_core)

# Node fixtures with the output of KotlinPoet 2.4.0 and 2.2.0 for the same specs.
uni_add_unit_test(kotlinpoet_test SOURCES ${_kp}/tests/kotlinpoet_test.c LIBRARIES kotlin_formatters_kotlinpoet)
