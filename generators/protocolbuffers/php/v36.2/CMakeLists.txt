uni_generator(LIBRARIES cpp_libprotoc)

# The test includes the unity build (generator.c) to reach its static functions.
uni_add_unit_test(php_v36_2_test SOURCES tests/generator_test.c LIBRARIES cpp_libprotoc uni_uni)
