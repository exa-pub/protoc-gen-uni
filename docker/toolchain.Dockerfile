# Build environment of protoc-gen-uni: musl cross compilers for the static Linux binaries,
# mingw-w64 for Windows, clang/lld (size comparison, sanitizers, libFuzzer), CMake, Ninja and
# qemu-user to run the aarch64 binary on x86_64 hosts.
#
#   docker build -t protoc-gen-uni-toolchain -f docker/toolchain.Dockerfile docker
#
# scripts/docker.sh builds (once) and runs this image; CI pulls it from ghcr.io.

ARG DEBIAN=debian:trixie-20251020-slim

FROM ${DEBIAN} AS cross
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential ca-certificates curl wget bzip2 xz-utils patch rsync file \
    && rm -rf /var/lib/apt/lists/*
# musl-cross-make: GCC + binutils + musl, the same versions for every target.
ARG MCM_COMMIT=5e8042e63de29f8473fc3b0b831a587ba284e1a8
ARG GCC_VER=14.3.0
ARG BINUTILS_VER=2.44
ARG MUSL_VER=1.2.6
RUN curl -fsSL https://github.com/richfelker/musl-cross-make/archive/${MCM_COMMIT}.tar.gz | tar -xz -C /tmp \
    && mv /tmp/musl-cross-make-${MCM_COMMIT} /mcm
WORKDIR /mcm
RUN printf '%s\n' \
        "GCC_VER = ${GCC_VER}" "BINUTILS_VER = ${BINUTILS_VER}" "MUSL_VER = ${MUSL_VER}" \
        "LINUX_VER =" "OUTPUT = /opt/cross" \
        'COMMON_CONFIG += CFLAGS="-g0 -O2" CXXFLAGS="-g0 -O2" LDFLAGS="-s"' \
        'GCC_CONFIG += --enable-languages=c --disable-multilib --disable-libquadmath --disable-libitm --disable-libsanitizer --disable-libgomp --disable-libssp' \
        > config.mak
RUN make -j"$(nproc)" TARGET=x86_64-linux-musl install && rm -rf build
RUN make -j"$(nproc)" TARGET=aarch64-linux-musl install && rm -rf build

FROM ${DEBIAN}
RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates curl git bash make cmake ninja-build python3 xz-utils file binutils \
        clang lld llvm libclang-rt-19-dev qemu-user gcc-mingw-w64-x86-64 \
    && rm -rf /var/lib/apt/lists/*
COPY --from=cross /opt/cross /opt/cross
ENV PATH=/opt/cross/bin:$PATH
