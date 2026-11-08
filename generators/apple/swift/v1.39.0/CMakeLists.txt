uni_generator(LIBRARIES swift_swift_protobuf)

# The test includes the unity build (generator.c) to reach its static functions.
uni_add_unit_test(swift_v1_39_0_test SOURCES tests/generator_test.c LIBRARIES swift_swift_protobuf uni_uni)
