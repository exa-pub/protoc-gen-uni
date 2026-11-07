# protoc-gen-uni

One compact protoc plugin that bundles byte-exact C ports of popular protobuf plugins — a single
≈500 KB binary, no Node.js, Go, Python or other runtimes needed. Pick the generator with `uni-name`
(as on buf.build) and `uni-version`; all other options go to the generator unchanged:

```yaml
# buf.gen.yaml
version: v2
plugins:
  - local: protoc-gen-uni
    out: gen/ts
    opt: [uni-name=bufbuild/es, uni-version=v2.17.0, target=ts]
  - local: protoc-gen-uni
    out: gen/go
    opt: [uni-name=protocolbuffers/go, uni-version=v1.36.12, paths=source_relative]
```

With protoc (binary on `PATH`):
`protoc --uni_out=gen/ts --uni_opt=uni-name=bufbuild/es,uni-version=v2.17.0,target=ts foo.proto`.

The output is identical, byte for byte, to that of the upstream plugin at that version: every
commit is tested against checksums of the buf.build remote plugin output. `protoc-gen-uni --list` prints the bundled
generators, `--version` the binary version.

## Generators

| uni-name | versions | upstream |
|----------|----------|----------|
| [`bufbuild/es`](generators/bufbuild/es/README.md) | v2.17.0 | [protoc-gen-es](https://buf.build/bufbuild/es) |
| [`protocolbuffers/go`](generators/protocolbuffers/go/README.md) | v1.36.12 | [protoc-gen-go](https://buf.build/protocolbuffers/go) |

## Install

Download the binary for your platform from
[Releases](https://github.com/exa-pub/protoc-gen-uni/releases)
(`protoc-gen-uni_<version>_{linux_amd64,linux_arm64,darwin_amd64,darwin_arm64,windows_amd64.exe}`,
sha256 in `checksums.txt`), rename it to `protoc-gen-uni` (`protoc-gen-uni.exe`), make it executable
and put it on your `PATH`. Linux binaries are fully static (musl) and run on any distribution.

## Building from source

C11, CMake ≥ 3.25 and Ninja; release binaries are built in a Docker toolchain image:

```bash
scripts/docker.sh cmake --workflow --preset linux-x86_64   # build/linux-x86_64/protoc-gen-uni + tests
cmake --workflow --preset dev                              # host compiler, debug
make test                                                  # both Linux architectures in Docker
```

See [CONTRIBUTING.md](CONTRIBUTING.md) for presets, tests, the project structure and how to add
generators.

## CI and releases

[`.github/workflows/ci.yml`](.github/workflows/ci.yml): `version` → toolchain image → `lint` +
`build` (Linux x86_64/aarch64 and Windows in the image, macOS arm64/x86_64 natively; reproducible:
built twice and compared) → tests (golden tests on Linux x86_64/arm64, macOS and Windows; ASan +
UBSan; 2 minutes of fuzzing) → `release`.

- **version** — the target version is fixed at the start of the run: latest `vX.Y.Z` tag + bump based on
  [conventional commits](https://www.conventionalcommits.org/) (`feat!:` / `BREAKING CHANGE:` → major,
  minor before 1.0; `feat:` → minor; anything else → patch). No tags yet → `v0.1.0`. Pull requests get
  `vX.Y.Z-g<sha>`.
- **release** — push to `main` only: the tested binaries for linux/darwin/windows plus
  `checksums.txt`, a `vX.Y.Z` tag and a GitHub Release with generated notes.

The version is derived from commit subjects on `main`, so squash-merge pull requests with a
conventional-commit title.
