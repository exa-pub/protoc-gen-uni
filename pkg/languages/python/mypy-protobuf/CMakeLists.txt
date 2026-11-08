# mypy-protobuf 5.1.0 (mypy_protobuf.h): mypy_protobuf/main.py, shared by its plugins
# protoc-gen-mypy (community/nipunn1313-mypy) and protoc-gen-mypy_grpc (community/nipunn1313-mypy-grpc).
add_library(python_mypy_protobuf STATIC main.c)
target_include_directories(python_mypy_protobuf PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(python_mypy_protobuf PUBLIC python_std uni_pb)
