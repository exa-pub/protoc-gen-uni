# Contributing

Read [CLAUDE.md](CLAUDE.md) first: its rules apply to every change. The details are in the script
headers this guide links to.

## Setup

- **Docker (recommended):** `scripts/docker.sh <command…>` runs a command in the toolchain image
  ([`docker/toolchain.Dockerfile`](docker/toolchain.Dockerfile)). The image has the musl cross GCC
  for x86_64 and aarch64, mingw-w64, clang, CMake and qemu-user. It is built on first use and takes
  a while.
- **Native:** CMake ≥ 3.25, Ninja, GCC or clang, bash and curl are enough for the `dev`, `asan`,
  `fuzz` and `darwin` presets.
- Tests download a pinned buf ([`scripts/get-buf.sh`](scripts/get-buf.sh)). The `es-validate`
  input needs network access for its buf dependencies.

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
  `UNI_WERROR`.
- **`make test|asan|fuzz|lint`** are shortcuts that run in the image.

ctest runs three kinds of tests:
- `unit/*`, the unit tests;
- `registry`, which checks that `--list`, `generators/`, the README version tables and
  `testdata/checksums` agree;
- `golden/<owner>/<plugin>/<version>/<test>`.

## Golden tests

[`tests/e2e/golden.sh`](tests/e2e/golden.sh) runs the binary through buf as a local plugin. It
uses the template's inputs and option groups and compares the sha256 of every output file with
`<version>.sha256`.

When a file differs, the test diffs it against the upstream output in `testdata/gen/`, if that
exists. Run `scripts/gen-testdata.sh` to create it. `UNI_E2E_WORK=<dir>` keeps our output.

## Test data

- **New input:** create `testdata/proto/<test>/`, a buf v2 workspace.
  - Prefix language-specific tests with the language, e.g. `go-features`.
  - Don't set `go_package`.
  - Add `- directory: testdata/proto/<test>` to the templates that should cover it.
- **New option group:** add a `- remote:` entry with its own `out:` and `opt:` to the template.
- **Regenerate:** `BUF_TOKEN=… scripts/gen-testdata.sh <owner>/<plugin> <version>`. This needs
  network access. Without a token, buf.build rate-limits almost immediately.
  - [The script header](scripts/gen-testdata.sh) lists the template constraints.
  - Commit only the template and `<version>.sha256`.
  - Reconfigure CMake afterwards: the golden test list is read from the templates.

## New version of a generator

1. Copy `generators/<owner>/<plugin>/<old>/` to `<new>/`.
   - In `generator.c`, rename the exported `gen_<owner>_<plugin>_<version>` symbol.
   - Update the version strings (`grep -rn <old>`).
2. Register the new version:
   - add the symbol to `generators/registry.c` and `generator.c` to `generators/CMakeLists.txt`;
   - add a `| <new> | … |` row to the README version table.
3. Copy `<old>.buf.gen.yaml` to `<new>.buf.gen.yaml`, change the version and run
   `scripts/gen-testdata.sh`.
4. Port the upstream diff until `golden/<owner>/<plugin>/<new>/*` pass. The old version must not
   change.

Go generators format code with the `go/printer` of the Go release they were built with. Check the
Go version in `plugins/<owner>/<plugin>/<version>/Dockerfile` of
[bufbuild/plugins](https://github.com/bufbuild/plugins). If it changed, port that standard library
to `pkg/languages/go/stdlib/go<version>/`
([README](pkg/languages/go/stdlib/go1.26.5/README.md)).

## New generator

1. Write `generators/<owner>/<plugin>/README.md`. It has: upstream sources at the tag, license,
   usage, version table, options and known differences.
2. Create `<version>/generator.c` exporting a `UniGenerator` ([`pkg/uni/uni.h`](pkg/uni/uni.h)).
   - The other `.c` files of the directory are `#include`d into it and stay `static`.
   - Register it like a new version.
3. Write the template and run `scripts/gen-testdata.sh`.
4. Port. Shared code goes to `pkg/languages/<lang>/` or `pkg/`.

## Checks

- **Sanitizers:** `scripts/docker.sh cmake --workflow --preset asan`. It turns off the arena, so
  ASan sees every object.
- **Fuzzing:** `make fuzz FUZZ_TIME=600`.
  - The harness is [`fuzz/fuzz_request.c`](fuzz/fuzz_request.c).
  - [`scripts/fuzz-corpus.sh`](scripts/fuzz-corpus.sh) records the seeds via buf.
  - Every finding is a bug.
- **Size:** compare `ls -l build/linux-*/protoc-gen-uni` before and after a change.
  - To attribute size to object files, build with
    `-DUNI_LTO=OFF -DCMAKE_EXE_LINKER_FLAGS=-Wl,-Map=<file>`.
  - Measurements of the optimization levels are in [`cmake/flags.cmake`](cmake/flags.cmake).
- **Lint:** `scripts/check-includes.sh` allows only libc headers.
- **CI** ([`.github/workflows/ci.yml`](.github/workflows/ci.yml)) builds every target twice and
  compares the binaries. It also runs the golden tests natively on Linux, macOS and Windows, plus
  ASan and fuzzing. It releases from `main`.

## Commits

Squash-merge PRs with a [conventional commit](https://www.conventionalcommits.org/) title. CI
derives the release version from it:
- `feat!:` or `BREAKING CHANGE:` bumps major, or minor before 1.0;
- `feat:` bumps minor;
- anything else bumps patch.
