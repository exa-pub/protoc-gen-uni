# protoc-gen-uni

One protoc plugin written in C11 that bundles **byte-exact C ports** of popular upstream plugins
(named as on buf.build: `<owner>/<plugin>`), each at an exact version. The generator is selected by
two parameters, everything else is passed to it unchanged:

```yaml
plugins:
  - local: protoc-gen-uni
    out: gen
    opt: [uni-name=bufbuild/es, uni-version=v2.17.0, target=ts]
```

## Key idea: compact, no extra dependencies

The whole point is a **single small binary without extra dependencies**: no Node.js, Python, Go,
protoc or buf at runtime, and no third-party C libraries — only the C library. Linux binaries are
fully static (musl, ≈500 KB with all generators); macOS and Windows link the system C runtime.
Whatever an upstream plugin needs (its plugin framework, protobuf runtime, Go's formatter) is ported
by hand into `pkg/` or into the generator itself. `scripts/check-includes.sh` (CI `lint`) allows only
C11/POSIX libc headers in `#include <...>`.

## Rules

- **Upstream is the reference.** `testdata/checksums/<owner>/<plugin>/<version>.sha256` (and the
  optional, uncommitted upstream output in `testdata/checksums/*/*/gen/`) are produced only by the
  upstream plugin (`scripts/gen-testdata.sh`, buf.build remote plugins). Never edit them or write
  them from our output; when a golden test fails, fix the port.
- **Output must match byte for byte**, including headers with the upstream version, option parsing,
  error texts, `supported_features` and supported editions.
- **Versions are frozen.** `generators/<owner>/<plugin>/<version>/` reproduces exactly that upstream
  version. A new upstream version = copy the previous version directory, rename the exported
  symbol, register it, and port the upstream diff. Changes to shared code in `pkg/` must keep golden
  tests of *all* versions green.
- Ported code keeps upstream copyright/license headers and names the upstream file it ports.
- Code and docs are English only.

## C conventions

- One request per process: everything is allocated from an arena (`xalloc`, `pkg/core/util.h`) and
  never freed. `-DUNI_NO_ARENA` (set by `UNI_SANITIZE`) gives every object its own block for ASan.
- Errors: `fail()` longjmps to the innermost `try_run()` (panic/recover); generation errors end up in
  `CodeGeneratorResponse.error` like upstream.
- Input is untrusted: every read is bounds-checked, recursion is limited (`PB_MAX_DEPTH`, stack
  budgets in protodesc and the Go parser). Bad input must give an error, never a crash or hang.
- `COLD` (`__attribute__((cold))`) on code that runs once per request or declaration: smaller binary.
- Builds stay warning-free with `-Werror` on GCC, clang and mingw; ASan/UBSan and the fuzzer stay
  clean.

## Layout

- `main.c` — `--version`, `--list` (`<name> <version>` lines); reads the request, `pkg/uni` dispatches.
- `pkg/core` — `util.h` (arena, `Buf`, `Vec`, `Map`, `Str`, quoting, base64, `fail`/`try_run`),
  `sys.h` (I/O).
- `pkg/pb` — hand-written protobuf wire format and schema tables of `descriptor.proto`/`plugin.proto`.
- `pkg/uni` — `UniGenerator`, `uni-name`/`uni-version` parsing, dispatch.
- `pkg/languages/<language>/…` — shared by the generators of one language:
  `ecmascript/{protobuf,protoplugin}`; `go/{genid,strs,tag,features,protodesc,protogen}` and
  `go/stdlib/go1.26.5` (Go 1.26.5 `go/parser` + `go/printer` & co. for gofmt-exact output).
- `generators/registry.{h,c}` — explicit list of all generators, sorted by name and version.
- `generators/<owner>/<plugin>/<version>/` — one port, a unity build: only `generator.c` is compiled
  (listed in `generators/CMakeLists.txt`); it `#include`s the other `.c` files and exports one
  `const UniGenerator gen_<owner>_<plugin>_<version with . → _>`.
- `generators/<owner>/<plugin>/README.md` — upstream links, versions table, options, differences.
- `tests/unit/` — unit tests (ctest `unit/*`); `tests/e2e/` — `golden.sh`, `all.sh`,
  `check-registry.sh`; `fuzz/` — libFuzzer harness.
- `testdata/proto/<test>/` — shared input buf workspaces.
- `testdata/checksums/<owner>/<plugin>/<version>.buf.gen.yaml` (committed) — `inputs:` = tests
  (`- directory: testdata/proto/<test>`), one `- remote: buf.build/<owner>/<plugin>:<version>` entry
  per option group (`out: <group>`, `opt:` as a one-line list).
- `…/<version>.sha256` (committed, written by `scripts/gen-testdata.sh`) — `<sha256>  <group>/<test>/<path>`.
- `…/gen/<version>/<group>/<test>/` — upstream output, **not committed**; diffs on failures.
- `CMakePresets.json`, `cmake/` (flags, toolchains, golden tests), `docker/toolchain.Dockerfile`.

See CONTRIBUTING.md for workflows.

## Commands

```bash
scripts/docker.sh cmake --workflow --preset linux-x86_64   # build + all tests in the toolchain image
scripts/docker.sh cmake --workflow --preset asan           # ASan + UBSan
cmake --workflow --preset dev                              # host compiler, Debug, -Werror
ctest --test-dir build/linux-x86_64 -R 'golden/bufbuild/es/v2.17.0/proto3'
tests/e2e/golden.sh build/linux-x86_64/protoc-gen-uni bufbuild/es v2.17.0 proto3
scripts/check-includes.sh                                  # lint
scripts/gen-testdata.sh bufbuild/es v2.17.0                # regenerate checksums (network, BUF_TOKEN)
make test | asan | fuzz | lint                             # shortcuts via scripts/docker.sh
```
