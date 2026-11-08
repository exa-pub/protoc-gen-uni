# Release image (CI job "docker"): the static Linux binary on an empty base, for linux/amd64 and
# linux/arm64. Build context: protoc-gen-uni-amd64 and protoc-gen-uni-arm64, the tested release
# binaries; docker buildx picks the one of the target platform.
FROM scratch
ARG TARGETARCH
COPY --chmod=755 protoc-gen-uni-${TARGETARCH} /protoc-gen-uni
USER 65534:65534
ENTRYPOINT ["/protoc-gen-uni"]
