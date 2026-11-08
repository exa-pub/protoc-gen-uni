# Port of ConnectPluginUtilities (connect-swift 1.2.3): options, output file names, method and
# service names and the Generator base class of connectrpc/swift and connectrpc/swift-mocks.
add_library(swift_connect_plugin_utilities STATIC extensions.c file_path_components.c generator.c
  generator_options.c)
target_include_directories(swift_connect_plugin_utilities PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(swift_connect_plugin_utilities PUBLIC swift_swift_protobuf)

uni_add_unit_test(connect_plugin_utilities_test SOURCES tests/connect_plugin_utilities_test.c
  LIBRARIES swift_connect_plugin_utilities)
