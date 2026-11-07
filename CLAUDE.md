# protoc-gen-uni

One protoc plugin in C11 that bundles **byte-exact ports** of upstream plugins, each at an exact
version, named as on buf.build. `uni-name` and `uni-version` select the generator; all other
options go to it unchanged:

```yaml
plugins:
  - local: protoc-gen-uni
    out: gen
    opt: [uni-name=bufbuild/es, uni-version=v2.17.0, target=ts]
```

The point is **one small binary without dependencies**: only the C library, static musl on Linux
(≈350 KB with all generators). Whatever an upstream plugin needs (plugin framework, protobuf
runtime, Go's formatter) is ported by hand into `pkg/` or the generator.

## Rules

- **Upstream is the reference.** Checksums and upstream output in `testdata/` come only from the
  upstream plugin (`scripts/gen-testdata.sh`). Never edit them or produce them from our output. If a
  golden test fails, fix the port.
- **Byte for byte**: headers with the upstream version, option parsing, error texts,
  `supported_features`, editions.
- **Versions are frozen**: `generators/<owner>/<plugin>/<version>/` is exactly that upstream
  version. `pkg/` has no version directories: a component implements the newest upstream version in
  use, and only a part that differs for an older one is versioned (`strconv_go1.21.6.c`, or a
  `<version>/` directory). Changes in `pkg/` must keep the golden tests of *all* versions green.
- **No third-party code**: only the libc headers allowed by `scripts/check-includes.sh`.
- Ported code keeps upstream license headers and names the upstream file it ports. Write code and
  docs in English.
- Input is untrusted: reads are bounds-checked and recursion is limited. Bad input must give an
  error, never a crash or hang. Builds stay clean with `-Werror`, ASan/UBSan and the fuzzer.

## C conventions

- Arena: one request per process, so memory comes from `xalloc` (`pkg/core/util.h`) and is never
  freed.
- Errors: `fail()` longjmps to the nearest `try_run()`, which works like panic/recover.
- Mark code that runs once per request or declaration with `COLD`; size matters.

## Structure

A port mirrors upstream library by library; the only deviations are formatters fed with nodes instead
of text and truly shared code in `pkg/`. Layout and how to apply it: [docs/LAYOUT.md](docs/LAYOUT.md).

## Commands

```bash
scripts/docker.sh cmake --workflow --preset linux-x86_64   # build + all tests (toolchain image)
scripts/docker.sh cmake --workflow --preset asan           # ASan + UBSan
ctest --test-dir build/linux-x86_64 -R golden/bufbuild/es/v2.17.0/proto3 --output-on-failure
scripts/gen-testdata.sh bufbuild/es v2.17.0                # upstream checksums (network, BUF_TOKEN)
```

Workflows (new version or generator, test data, fuzzing, size) are in CONTRIBUTING.md.
