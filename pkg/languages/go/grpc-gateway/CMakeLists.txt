# Port of the shared packages of github.com/grpc-ecosystem/grpc-gateway/v2 v2.33.0 (internal/casing,
# internal/httprule, utilities, internal/descriptor with bufbuild/plugins'
# separate_pkg_additional_imports.patch, internal/codegenerator, protoc-gen-openapiv2/options): the base of its plugins
# (grpc-ecosystem/gateway, grpc-ecosystem/openapiv2). See grpcgateway.h.
add_library(go_grpc_gateway STATIC
  casing/camel.c
  httprule/parse.c httprule/types.c httprule/compile.c
  utilities/trie.c
  descriptor/registry.c descriptor/services.c descriptor/types.c descriptor/buf_build.c
  descriptor/grpc_api_configuration.c
  codegenerator.c
  options/openapiv2.c)
target_include_directories(go_grpc_gateway PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(go_grpc_gateway PUBLIC go_protogen go_genproto go_std)

uni_add_unit_test(go_grpc_gateway_test
  SOURCES grpcgateway_test.c
  LIBRARIES go_grpc_gateway)
