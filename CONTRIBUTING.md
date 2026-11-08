# Contributing

Read [docs/GOALS.md](docs/GOALS.md) and [docs/RULES.md](docs/RULES.md) first: the rules apply to
every change. The layout is in [docs/LAYOUT.md](docs/LAYOUT.md); the details are in the script
headers this guide links to.

## Setup

- **Docker (recommended):** `scripts/docker.sh <command…>` runs a command in the toolchain image
  ([`docker/toolchain.Dockerfile`](docker/toolchain.Dockerfile)). The image has the musl cross GCC
  for x86_64 and aarch64, mingw-w64, clang, CMake and qemu-user. It is built on first use and takes
  a while.
- **Native:** CMake ≥ 3.28, Ninja, GCC or clang, bash and curl are enough for the `dev`, `asan`,
  `fuzz` and `darwin` presets.
- Tests download a pinned buf ([`scripts/get-buf.sh`](scripts/get-buf.sh)). Inputs with buf
  dependencies (`deps:` in their `buf.yaml`, e.g. `es-validate`, `gnostic-openapi`,
  `http-gateway*`) need network access.

## Build and test

```bash
scripts/docker.sh cmake --workflow --preset linux-x86_64     # or linux-aarch64 (tests under qemu)
cmake --workflow --preset dev                                # natively, Debug, -Werror
ctest --test-dir build/linux-x86_64 -L golden                # -L unit, -R <regex>
tests/e2e/golden.sh build/linux-x86_64/protoc-gen-uni bufbuild/es v2.17.0 proto3
```

- **Presets** ([`CMakePresets.json`](CMakePresets.json), output in `build/<preset>/`):
  `linux-{x86_64,aarch64}[-clang]`, `windows-x86_64`, `darwin`, `dev`, `asan`, `fuzz`.
- **Options** are in [`cmake/flags.cmake`](cmake/flags.cmake), for example `UNI_OPT` and
  `UNI_WERROR`. `-DUNI_GENERATORS=bufbuild/es,protocolbuffers/go/v1.36.12` builds a binary with
  only those generators; golden tests of the others are skipped, and unit tests of libraries they
  do not use are neither built nor run.
- **`make test|asan|fuzz|lint`** are shortcuts that run in the image.

ctest runs three kinds of tests:
- `unit/*`, the unit tests;
- `registry`, which checks that `--list`, `generators/`, the README version tables and
  `testdata/checksums` agree;
- `golden/<owner>/<plugin>/<version>/<test>`.

## Golden tests

[`tests/e2e/golden.sh`](tests/e2e/golden.sh) runs the binary through buf as a local plugin. It
uses the template's inputs and option groups. For each test it compares one sum over the output of
all option groups with the line `<sum>  <version>/<test>` in
`testdata/checksums/<owner>/<plugin>/checksums.txt`. [The gen-testdata.sh
header](scripts/gen-testdata.sh) defines the sum.

When the sum differs, the test lists the missing, extra and differing files and diffs them against
the upstream output in `testdata/gen/`, if that exists. Run `scripts/gen-testdata.sh` to create it.
`UNI_E2E_WORK=<dir>` keeps our output.

## Test data

- **New input:** create `testdata/proto/<test>/`, a buf v2 workspace.
  - Prefix language-specific tests with the language, e.g. `go-features`.
  - Don't set `go_package`.
  - A real project is `testdata/proto/real-<name>/`: an unmodified copy with its `LICENSE` and a
    `SOURCE` file (repository, commit, what was taken).
  - Add `- directory: testdata/proto/<test>` to the templates that should cover it.
- **New option group:** add a `- remote:` entry with its own `out:` and `opt:` to the template.
  - A group may fail upstream (an invalid option, an unsupported input). Its error text is then
    tested byte for byte instead of files, as the line `<group>:error` of the test sum (see the
    script header).
- **Regenerate:** `BUF_TOKEN=… scripts/gen-testdata.sh <owner>/<plugin> <version>`. This needs
  network access. Without a token, buf.build rate-limits almost immediately.
  - [The script header](scripts/gen-testdata.sh) lists the template constraints.
  - It replaces the lines of that version in `checksums.txt` and rewrites `testdata/gen/`.
  - Commit only the template and `checksums.txt`.
  - Reconfigure CMake afterwards: the golden test list is read from the templates.

## New version of a generator

1. Create `generators/<owner>/<plugin>/<new>/` with `CMakeLists.txt` and `generator.c`. Its
   exported symbol is the directory path as a C identifier, `gen_<owner>_<plugin>_<version>` (CMake
   discovers the directory and generates the registry).
   - A version costs its difference, not a copy: code the versions share lives in
     `generators/<owner>/<plugin>/common/` (move it there when the second version appears), and
     `<new>/` holds only what differs.
2. Add a `| <new> | … |` row to the README version table.
3. Copy `<old>.buf.gen.yaml` to `<new>.buf.gen.yaml`, change the version and run
   `scripts/gen-testdata.sh`.
4. Port the upstream diff until `golden/<owner>/<plugin>/<new>/*` pass. The old version must not
   change.

Go generators format code with the `go/printer` of the Go release they were built with. Check the
Go version in `plugins/<owner>/<plugin>/<version>/Dockerfile` of
[bufbuild/plugins](https://github.com/bufbuild/plugins). If the output differs from the Go standard
library port in `pkg/languages/go/`, version the differing files; check the release notes for
Unicode, strconv and sort changes.

## New generator

1. Write `generators/<owner>/<plugin>/README.md`. It has: upstream sources at the tag, license,
   usage, version table, options and known differences.
2. Create `<version>/generator.c` exporting a `UniGenerator` ([`pkg/uni/uni.h`](pkg/uni/uni.h)).
   - The other `.c` files of the directory are `#include`d into it and stay `static`.
   - Add `<version>/CMakeLists.txt` with `uni_generator(LIBRARIES <libraries it uses>)`.
   - Set `parses_raw` when the generator parses the raw request itself (protobuf-go: 10000
     levels of nesting) and reads only the parameter from `req`: requests nested deeper than the
     100 levels of `pkg/pb` then reach it instead of failing in `uni_main`.
3. Write the template and run `scripts/gen-testdata.sh`.
4. Port, following [docs/LAYOUT.md](docs/LAYOUT.md): upstream libraries to `pkg/languages/<language>/<library>/`,
   formatters to `pkg/languages/<language>/formatters/<name>/`, code shared by versions to `common/`.

## Checks

- **Sanitizers:** `scripts/docker.sh cmake --workflow --preset asan`. It turns off the arena, so
  ASan sees every object.
- **Fuzzing:** `make fuzz FUZZ_TIME=600`.
  - The harness is [`fuzz/fuzz_request.c`](fuzz/fuzz_request.c).
  - [`scripts/fuzz-corpus.sh`](scripts/fuzz-corpus.sh) records the seeds via buf.
  - Every finding is a bug. Findings (`crash-*`, `oom-*`, `timeout-*`) are written to `build/fuzz/`;
    `slow-unit-*` is an input slower than 10 s under the sanitizers, not a finding by itself.
- **Size:** compare `ls -l build/linux-*/protoc-gen-uni` before and after a change.
  - To attribute size to object files, build with
    `-DUNI_LTO=OFF -DCMAKE_EXE_LINKER_FLAGS=-Wl,-Map=<file>`.
  - Measurements of the optimization levels are in [`cmake/flags.cmake`](cmake/flags.cmake).
- **Lint:** `scripts/check-includes.sh` allows only libc headers.
- **CI** ([`.github/workflows/ci.yml`](.github/workflows/ci.yml)) builds every target twice and
  compares the binaries. It also runs the golden tests natively on Linux, macOS and Windows, plus
  ASan and fuzzing, then builds the `FROM scratch` image ([`docker/scratch.Dockerfile`](docker/scratch.Dockerfile))
  for linux/amd64 and linux/arm64. It releases the binaries and pushes the image from `main`.

## Commits

Squash-merge PRs with a [conventional commit](https://www.conventionalcommits.org/) title. CI
derives the release version from it:
- `feat!:` or `BREAKING CHANGE:` bumps major, or minor before 1.0;
- `feat:` bumps minor;
- anything else bumps patch.
