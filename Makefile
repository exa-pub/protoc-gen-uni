# Shortcuts; everything runs in the toolchain image (scripts/docker.sh), see CONTRIBUTING.md.
DOCKER := scripts/docker.sh

.PHONY: all build test asan fuzz lint testdata clean

all: lint test

# Static binaries: build/linux-x86_64/protoc-gen-uni, build/linux-aarch64/protoc-gen-uni
build:
	$(DOCKER) bash -c 'for p in linux-x86_64 linux-aarch64; do cmake --preset $$p && cmake --build --preset $$p || exit 1; done'

# Build + unit, registry and golden tests on both architectures (aarch64 under qemu)
test:
	$(DOCKER) cmake --workflow --preset linux-x86_64
	$(DOCKER) cmake --workflow --preset linux-aarch64

# All tests under ASan + UBSan
asan:
	$(DOCKER) cmake --workflow --preset asan

# FUZZ_TIME seconds of libFuzzer, seeded with the requests of the golden tests
FUZZ_TIME ?= 60
fuzz:
	$(DOCKER) bash -c 'cmake --preset fuzz && cmake --build --preset fuzz && scripts/fuzz-corpus.sh && \
		build/fuzz/fuzz/fuzz_request -max_total_time=$(FUZZ_TIME) -timeout=10 -rss_limit_mb=2048 \
		-dict=fuzz/params.dict build/fuzz/corpus'

lint:
	scripts/check-includes.sh

# make testdata NAME=bufbuild/es VERSION_GEN=v2.17.0  (needs network and BUF_TOKEN)
testdata:
	scripts/gen-testdata.sh $(NAME) $(VERSION_GEN)

clean:
	rm -rf build dist
