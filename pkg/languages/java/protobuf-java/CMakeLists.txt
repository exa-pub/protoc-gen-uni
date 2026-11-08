# protobuf-java 3.25.8 and the differences of 4.36.2 (protobuf_java.h): Descriptors, feature resolution,
# GeneratorNames and message serialization, as the JVM plugins
# built on it use them (ScalaPB: pkg/languages/scala/scalapb; grpc/kotlin: 3.25; connectrpc/kotlin: 4.36.2).
add_library(java_protobuf_java STATIC descriptors.c features.c generator_names.c message.c)
target_include_directories(java_protobuf_java PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(java_protobuf_java PUBLIC java_std uni_pb)
