# Contributing

protoc-gen-uni bundles byte-exact C ports of upstream protoc plugins into one compact binary.
Read [CLAUDE.md](CLAUDE.md) first: the rules there (no third-party dependencies, upstream is the
reference, frozen versions) apply to every change.

## Requirements

- **Docker** — the recommended way: `scripts/docker.sh <command...>` runs a command in the toolchain
  image ([`docker/toolchain.Dockerfile`](docker/toolchain.Dockerfile): GCC + musl cross compilers
  for x86_64/aarch64 from musl-cross-make, mingw-w64, clang/lld, CMake, Ninja, qemu-user). The image
  is tagged with a hash of the Dockerfile and built locally on first use (slow: GCC is compiled for
  every target); `UNI_TOOLCHAIN_IMAGE=<image>` uses another one, e.g. the one CI pushes to ghcr.io.
  The repository is mounted at the same path with your uid, so build trees are usable on the host.
- **Native** — CMake ≥ 3.25, Ninja, a C11 compiler (GCC or clang), bash, curl. Enough for the `dev`,
  `asan` (clang), `fuzz` (clang) and `darwin` presets.
- Tests download a pinned buf (`scripts/get-buf.sh`, v1.75.0, sha256-verified, into
  `build/<preset>/tools`); `-DUNI_BUF=<path>` or `BUF=<path>` uses your own. Some test inputs
  (`es-validate`) need network access for buf dependencies.
- Regenerating test data: network access to buf.build and `BUF_TOKEN` (unauthenticated remote
  plugins are rate limited almost immediately).

## Building

Presets ([`CMakePresets.json`](CMakePresets.json)) build into `build/<preset>/`:

| preset | what |
|--------|------|
| `linux-x86_64`, `linux-aarch64` | release binaries: static musl, GCC (toolchain image); aarch64 tests run under qemu-user |
| `linux-x86_64-clang`, `linux-aarch64-clang` | the same with clang + lld (size comparison) |
| `windows-x86_64` | mingw-w64 (toolchain image); `protoc-gen-uni.exe` |
| `darwin` | macOS, host clang |
| `dev` | host compiler, Debug, `-Werror` |
| `asan` | host clang, Debug, ASan + UBSan |
| `fuzz` | host clang, libFuzzer + ASan + UBSan (`fuzz/fuzz_request.c`) |

```bash
scripts/docker.sh cmake --workflow --preset linux-x86_64   # configure + build + test
scripts/docker.sh cmake --preset windows-x86_64 && scripts/docker.sh cmake --build --preset windows-x86_64
cmake --workflow --preset dev                              # natively
cmake --preset darwin && cmake --build --preset darwin && ctest --preset darwin   # macOS
```

Workflow presets exist for `linux-x86_64`, `linux-aarch64`, `dev` and `asan`. The `Makefile` wraps
the common ones in the toolchain image: `make build`, `make test` (both Linux architectures),
`make asan`, `make fuzz`, `make lint`, `make testdata NAME=<owner>/<plugin> VERSION_GEN=<version>`,
`make clean`.

Cache options ([`cmake/flags.cmake`](cmake/flags.cmake)): `UNI_VERSION` (printed by `--version`,
default `dev`), `UNI_OPT` (`Os` default, `O2`, `O3`), `UNI_LTO` (default `ON`), `UNI_WERROR`, `UNI_HARDENING` (stack
protector + RELRO), `UNI_SANITIZE`, `UNI_GOFMT_OPT` (optimization level of the Go formatter
translation unit only, an experiment). Release builds use LTO, `-ffunction-sections` +
`--gc-sections`, no unwind tables, `-fno-pie` and a stripped static link on Linux. Builds are
reproducible: CI builds every binary twice and compares them with `cmp`.

## Tests

`ctest --test-dir build/<preset>` (or the test step of a workflow preset) runs:

- `unit/*` — `tests/unit/*_test.c` (`core`, `pb`, `uni`, `protoplugin`, `go`; helpers in
  `tests/unit/test.h`) and the Go formatter tests in `pkg/languages/go/stdlib/go1.26.5/tests/`.
  `unit/go1_26_5_corpus*` are skipped unless the corpus `build/gofmt-corpus` exists (see
  [its README](pkg/languages/go/stdlib/go1.26.5/README.md)).
- `registry` — [`tests/e2e/check-registry.sh`](tests/e2e/check-registry.sh): the binary's `--list`,
  `generators/<owner>/<plugin>/<version>/generator.c`, the versions table rows (`| vX.Y.Z | … |`) of
  the generator README, `testdata/checksums/<owner>/<plugin>/<version>.{buf.gen.yaml,sha256}` and
  the `testdata/proto/<test>` inputs must all agree.
- `golden/<owner>/<plugin>/<version>/<test>` — one per template input, see below.

Labels: `ctest -L unit`, `ctest -L golden`. A single golden test:

```bash
ctest --test-dir build/linux-x86_64 -R 'golden/bufbuild/es/v2.17.0/proto3' --output-on-failure
tests/e2e/golden.sh build/linux-x86_64/protoc-gen-uni bufbuild/es v2.17.0 proto3
tests/e2e/all.sh build/linux-x86_64/protoc-gen-uni     # every template, as CI does on each OS
```

### How golden tests work

[`tests/e2e/golden.sh [--runner CMD] BIN NAME VERSION [TEST...]`](tests/e2e/golden.sh) takes
`testdata/checksums/<name>/<version>.buf.gen.yaml`, replaces every `remote:` entry by the binary as
a `local:` plugin with `strategy: all` (one request with all files, like remote plugins) and
prepends `uni-name=<name>, uni-version=<version>` to its `opt:`. buf then runs it on each
`testdata/proto/<test>`, and the sha256 of every output file is compared with the
`<sha>  <group>/<test>/<path>` lines of `<version>.sha256` (missing, extra and differing files).

On a mismatch, if `testdata/checksums/<name>/gen/<version>/` exists (written by
`scripts/gen-testdata.sh`, not committed), differing files are diffed against it
(`UNI_DIFF_LINES`, default 60 lines per file); if that directory does not match the checksums, the
test says it is stale. Without it you only get "differs" — run the script to get diffs.
`UNI_E2E_WORK=<dir>` keeps our output in `<dir>/<test>/<group>/`. Exit status 77 (skipped) means
the binary does not contain that generator version. `--runner qemu-aarch64` runs a cross-built
binary; CMake passes `CMAKE_CROSSCOMPILING_EMULATOR` automatically.

Template constraints (checked by `scripts/gen-testdata.sh` and `golden.sh`): every plugin entry is
`- remote: buf.build/<owner>/<plugin>:<version>` with a plain `out: <group>` and, if present, a
non-empty one-line `opt: [...]`; no `strategy:` (buf rejects it for remote plugins); every input is
`- directory: testdata/proto/<test>`. Managed mode applies to the whole template and is part of the
request; use it only where upstream output does not embed the changed options.

## Adding a test input or option group

1. Create `testdata/proto/<test>/` as a buf workspace (`buf.yaml`, `version: v2`). Language-specific
   tests are prefixed with the language (`go-features`, `es-validate`). Do not set `go_package`
   (the Go template uses managed mode).
2. Add `- directory: testdata/proto/<test>` to `inputs:` (or a new `- remote:` entry with its own
   `out:` and `opt:`) of every generator version template that should cover it.
3. Regenerate the test data of those versions (next section), reconfigure CMake (the golden test
   list is read from the templates) and run the tests.

## Regenerating test data

```bash
BUF_TOKEN=... scripts/gen-testdata.sh <owner>/<plugin> <version>
```

[`scripts/gen-testdata.sh`](scripts/gen-testdata.sh) runs `buf generate testdata/proto/<test>`
with the template for every input — the upstream remote plugins produce the output — writes it to
`testdata/checksums/<owner>/<plugin>/gen/<version>/<group>/<test>/` and its checksums to
`<version>.sha256`. Commit only the template and `<version>.sha256`. Never edit checksums by hand or
produce them from our output.

## Adding a new version of an existing generator

1. Copy `generators/<owner>/<plugin>/<old>/` to `<new>/`. Rename the exported symbol in
   `generator.c` (`gen_<owner>_<plugin>_<new with . → _>`) and update the version strings
   (`grep -rn '<old>' generators/<owner>/<plugin>/<new>/`, e.g. `.version` in es `plugin.c`,
   `Version` in go `main.c`).
2. Add the symbol to `generators/registry.c` (sorted by name, then version) and
   `<new>/generator.c` to `generators/CMakeLists.txt`.
3. Add a `| <new> | … |` row to the versions table in `generators/<owner>/<plugin>/README.md`.
4. Copy `testdata/checksums/<owner>/<plugin>/<old>.buf.gen.yaml` to `<new>.buf.gen.yaml`, replace
   the version, run `scripts/gen-testdata.sh <owner>/<plugin> <new>`.
5. Port the upstream diff between the two tags until all `golden/<owner>/<plugin>/<new>/*` tests
   pass. Do not change the old version's behavior; if shared code in `pkg/` must change, the golden
   tests of all versions must stay green.

Go generators format code with Go's `go/printer`, whose output changes between Go releases. Check
the Go version in `plugins/<owner>/<plugin>/<version>/Dockerfile` of
[bufbuild/plugins](https://github.com/bufbuild/plugins); if it changed, port that standard library
as a new `pkg/languages/go/stdlib/go<version>/` (see
[its README](pkg/languages/go/stdlib/go1.26.5/README.md)).

## Adding a new generator

1. `generators/<owner>/<plugin>/README.md`: upstream repository, exact source paths at the tag,
   buf.build page, license, usage, versions table, options, known differences.
2. `generators/<owner>/<plugin>/<version>/generator.c` exporting
   `const UniGenerator gen_<owner>_<plugin>_<version> = {"<owner>/<plugin>", "<version>", generate};`
   (see `pkg/uni/uni.h`; copy the structure of an existing generator). Other `.c` files of the
   directory are `#include`d by `generator.c` and keep everything `static`. Register it in
   `generators/registry.c` and `generators/CMakeLists.txt` (with the libraries it links).
3. `testdata/checksums/<owner>/<plugin>/<version>.buf.gen.yaml` with `inputs:` (tests) and option
   groups covering the plugin options, then `scripts/gen-testdata.sh`.
4. Port. Shared language helpers go to `pkg/languages/<language>/` (with a `CMakeLists.txt` added to
   the root `CMakeLists.txt`), other shared code to `pkg/`. Keep upstream copyright headers on ported
   code and name the upstream source file in a comment.

## Dependencies

Only the C library. `scripts/check-includes.sh` (CI `lint`, `make lint`) fails on any
`#include <...>` outside the allowed C11/POSIX set; project headers are included with `"..."`.
Need something from another library? Port the minimal part into `pkg/`. Dev-only helper scripts
may use other tools (Go for `scripts/gen-isprint.sh`, `tests/unit/gen_go_cases.go` and the Go
formatter corpus), but nothing they need is linked into the binary.

## Sanitizers and fuzzing

```bash
scripts/docker.sh cmake --workflow --preset asan          # all tests under ASan + UBSan
scripts/docker.sh bash -c 'cmake --preset fuzz && cmake --build --preset fuzz &&
  scripts/fuzz-corpus.sh &&
  build/fuzz/fuzz/fuzz_request -max_total_time=600 -dict=fuzz/params.dict build/fuzz/corpus'
```

`UNI_SANITIZE` disables the arena (`UNI_NO_ARENA`: one allocation per object), so ASan sees
out-of-bounds accesses. [`fuzz/fuzz_request.c`](fuzz/fuzz_request.c) takes
`<parameter>\n<CodeGeneratorRequest>` inputs, has a structure-aware mutator and checks that every
input yields a parse error or a well-formed response. `scripts/fuzz-corpus.sh [NAME VERSION [OUT]]`
records seed requests via buf for every option group and test of the templates (default
`build/fuzz/corpus`). Findings are bugs: fix them and add a unit test where it makes sense.

## Binary size

Size is a feature: check `ls -l build/linux-*/protoc-gen-uni` before and after a change
(currently ≈349 KB x86_64, ≈358 KB aarch64). Mark code that runs once per request or declaration
`COLD`, avoid stdio (`buf_fmt` replaces `printf`), prefer tables and text templates over repeated
code. `linux-*-clang` presets, `-DUNI_OPT=O3`, `-DUNI_LTO=OFF` and `-DUNI_GOFMT_OPT=…` help to compare
(with `-DUNI_LTO=OFF -DCMAKE_EXE_LINKER_FLAGS=-Wl,-Map=<file>` the link map attributes the size to
object files); the
measurements for protocolbuffers/go are in its [README](generators/protocolbuffers/go/README.md).

## CI

[`.github/workflows/ci.yml`](.github/workflows/ci.yml): `version` → `toolchain` image (ghcr.io,
tagged by the Dockerfile hash) → `lint` (`check-includes.sh`), `build` (`linux-x86_64`,
`linux-aarch64`, `windows-x86_64` in the image, `-Werror`, built twice + `cmp`, ctest for Linux),
`build-darwin` (arm64, x86_64) → `test-linux` (x86_64 and arm64 runners), `test-darwin`,
`test-windows` (Git Bash) with `tests/e2e/all.sh`, `asan`, `fuzz` (2 minutes) → `release` on `main`.

## Commits

Squash-merge PRs with a [conventional commit](https://www.conventionalcommits.org/) title — CI
derives the release version from it (`feat!:` / `BREAKING CHANGE:` → major, minor before 1.0;
`feat:` → minor; otherwise patch).
