# libFuzzer harness (configure preset "fuzz"; see fuzz_request.c and scripts/fuzz-corpus.sh).
add_executable(fuzz_request fuzz_request.c)
target_link_libraries(fuzz_request PRIVATE uni_generators)
target_link_options(fuzz_request PRIVATE -fsanitize=fuzzer)
