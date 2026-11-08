# protoc-gen-uni

One compact protoc plugin that bundles byte-exact C ports of popular protobuf plugins — a single
≈3.3 MB binary, no Node.js, Go, Python or other runtimes needed. Pick the generator with `uni-name`
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

- ✅ the same output as the upstream plugin, with every option.
- ✳️ the same, and the formatter upstream runs on its output (dart_style, prettyplease, KotlinPoet)
  is ported, so line breaks match for any name or comment length.
- ⚠️ some options or ordinary inputs are not supported: generation fails with an error that names
  them; everything else is the same as upstream.

Each generator's README lists what it does not support and how it differs. Differences that do not
change the mark: how errors are reported, invalid descriptors, and inputs that real projects do not
have (NUL bytes, unusual Unicode in names and option values, extreme nesting or sizes).

| | uni-name | versions | upstream |
|:-:|----------|----------|----------|
| ⚠️ | [`anthropics/buffa`](generators/anthropics/buffa/README.md) | v0.9.2 | [protoc-gen-buffa](https://buf.build/anthropics/buffa) |
| ✅ | [`apple/swift`](generators/apple/swift/README.md) | v1.39.0 | [protoc-gen-swift](https://buf.build/apple/swift) |
| ✅ | [`bufbuild/es`](generators/bufbuild/es/README.md) | v2.17.0 | [protoc-gen-es](https://buf.build/bufbuild/es) |
| ✅ | [`bufbuild/grpc-py`](generators/bufbuild/grpc-py/README.md) | v0.5.0 | [protoc-gen-grpc-py](https://buf.build/bufbuild/grpc-py) |
| ✅ | [`bufbuild/py`](generators/bufbuild/py/README.md) | v0.7.0 | [protoc-gen-py](https://buf.build/bufbuild/py) |
| ⚠️ | [`community/danielgtaylor-betterproto`](generators/community/danielgtaylor-betterproto/README.md) | v1.2.5 | [protoc-gen-python_betterproto](https://buf.build/community/danielgtaylor-betterproto) |
| ✅ | [`community/google-gnostic-openapi`](generators/community/google-gnostic-openapi/README.md) | v0.7.1 | [protoc-gen-openapi](https://buf.build/community/google-gnostic-openapi) |
| ✅ | [`community/mfridman-go-json`](generators/community/mfridman-go-json/README.md) | v1.6.0 | [protoc-gen-go-json](https://buf.build/community/mfridman-go-json) |
| ⚠️ | [`community/nanopb`](generators/community/nanopb/README.md) | v0.4.91 | [protoc-gen-nanopb](https://buf.build/community/nanopb) |
| ✅ | [`community/neoeinstein-prost`](generators/community/neoeinstein-prost/README.md) | v0.5.0 | [protoc-gen-prost](https://buf.build/community/neoeinstein-prost) |
| ⚠️ | [`community/neoeinstein-prost-crate`](generators/community/neoeinstein-prost-crate/README.md) | v0.5.0 | [protoc-gen-prost-crate](https://buf.build/community/neoeinstein-prost-crate) |
| ✅ | [`community/neoeinstein-prost-serde`](generators/community/neoeinstein-prost-serde/README.md) | v0.4.0 | [protoc-gen-prost-serde](https://buf.build/community/neoeinstein-prost-serde) |
| ⚠️ | [`community/neoeinstein-tonic`](generators/community/neoeinstein-tonic/README.md) | v0.5.0 | [protoc-gen-tonic](https://buf.build/community/neoeinstein-tonic) |
| ⚠️ | [`community/nipunn1313-mypy`](generators/community/nipunn1313-mypy/README.md) | v5.1.0 | [protoc-gen-mypy](https://buf.build/community/nipunn1313-mypy) |
| ⚠️ | [`community/nipunn1313-mypy-grpc`](generators/community/nipunn1313-mypy-grpc/README.md) | v5.1.0 | [protoc-gen-mypy_grpc](https://buf.build/community/nipunn1313-mypy-grpc) |
| ✅ | [`community/planetscale-vtprotobuf`](generators/community/planetscale-vtprotobuf/README.md) | v0.6.0 | [protoc-gen-go-vtproto](https://buf.build/community/planetscale-vtprotobuf) |
| ✅ | [`community/protobuf-c`](generators/community/protobuf-c/README.md) | v1.5.2 | [protoc-gen-c](https://buf.build/community/protobuf-c) |
| ✅ | [`community/roadrunner-server-php-grpc`](generators/community/roadrunner-server-php-grpc/README.md) | v6.0.0 | [protoc-gen-php-grpc](https://buf.build/community/roadrunner-server-php-grpc) |
| ⚠️ | [`community/scalapb-scala`](generators/community/scalapb-scala/README.md) | v0.11.21 | [protoc-gen-scala](https://buf.build/community/scalapb-scala) |
| ⚠️ | [`community/scalapb-zio-grpc`](generators/community/scalapb-zio-grpc/README.md) | v0.6.4 | [protoc-gen-zio](https://buf.build/community/scalapb-zio-grpc) |
| ⚠️ | [`community/stephenh-ts-proto`](generators/community/stephenh-ts-proto/README.md) | v2.18.1 | [protoc-gen-ts_proto](https://buf.build/community/stephenh-ts-proto) |
| ⚠️ | [`community/timostamm-protobuf-ts`](generators/community/timostamm-protobuf-ts/README.md) | v2.11.1 | [protoc-gen-ts](https://buf.build/community/timostamm-protobuf-ts) |
| ✅ | [`connectrpc/dart`](generators/connectrpc/dart/README.md) | v2.0.1 | [protoc-gen-connect-dart](https://buf.build/connectrpc/dart) |
| ✅ | [`connectrpc/go`](generators/connectrpc/go/README.md) | v2.0.2 | [protoc-gen-connect-go](https://buf.build/connectrpc/go) |
| ✅ | [`connectrpc/gosimple`](generators/connectrpc/gosimple/README.md) | v1.21.1 | [protoc-gen-connect-go](https://buf.build/connectrpc/gosimple) |
| ✳️ | [`connectrpc/kotlin`](generators/connectrpc/kotlin/README.md) | v0.10.0 | [protoc-gen-connect-kotlin](https://buf.build/connectrpc/kotlin) |
| ✅ | [`connectrpc/py`](generators/connectrpc/py/README.md) | v0.13.0 | [protoc-gen-connectrpc](https://buf.build/connectrpc/py) |
| ✅ | [`connectrpc/query-es`](generators/connectrpc/query-es/README.md) | v2.3.1 | [protoc-gen-connect-query](https://buf.build/connectrpc/query-es) |
| ✳️ | [`connectrpc/rust`](generators/connectrpc/rust/README.md) | v0.9.0 | [protoc-gen-connect-rust](https://buf.build/connectrpc/rust) |
| ✅ | [`connectrpc/swift`](generators/connectrpc/swift/README.md) | v1.2.3 | [protoc-gen-connect-swift](https://buf.build/connectrpc/swift) |
| ✅ | [`connectrpc/swift-mocks`](generators/connectrpc/swift-mocks/README.md) | v1.2.3 | [protoc-gen-connect-swift-mocks](https://buf.build/connectrpc/swift-mocks) |
| ✅ | [`grpc-ecosystem/gateway`](generators/grpc-ecosystem/gateway/README.md) | v2.33.0 | [protoc-gen-grpc-gateway](https://buf.build/grpc-ecosystem/gateway) |
| ⚠️ | [`grpc-ecosystem/openapiv2`](generators/grpc-ecosystem/openapiv2/README.md) | v2.33.0 | [protoc-gen-openapiv2](https://buf.build/grpc-ecosystem/openapiv2) |
| ✅ | [`grpc/cpp`](generators/grpc/cpp/README.md) | v1.85.1 | [grpc_cpp_plugin](https://buf.build/grpc/cpp) |
| ✅ | [`grpc/csharp`](generators/grpc/csharp/README.md) | v1.85.1 | [grpc_csharp_plugin](https://buf.build/grpc/csharp) |
| ✅ | [`grpc/go`](generators/grpc/go/README.md) | v1.6.2 | [protoc-gen-go-grpc](https://buf.build/grpc/go) |
| ✅ | [`grpc/java`](generators/grpc/java/README.md) | v1.85.1 | [protoc-gen-grpc-java](https://buf.build/grpc/java) |
| ✳️ | [`grpc/kotlin`](generators/grpc/kotlin/README.md) | v1.5.0 | [protoc-gen-grpc-kotlin](https://buf.build/grpc/kotlin) |
| ✅ | [`grpc/node`](generators/grpc/node/README.md) | v1.13.1 | [grpc_node_plugin](https://buf.build/grpc/node) |
| ✅ | [`grpc/objc`](generators/grpc/objc/README.md) | v1.85.1 | [grpc_objective_c_plugin](https://buf.build/grpc/objc) |
| ✅ | [`grpc/php`](generators/grpc/php/README.md) | v1.85.1 | [grpc_php_plugin](https://buf.build/grpc/php) |
| ✅ | [`grpc/python`](generators/grpc/python/README.md) | v1.85.1 | [grpc_python_plugin](https://buf.build/grpc/python) |
| ✅ | [`grpc/ruby`](generators/grpc/ruby/README.md) | v1.85.1 | [grpc_ruby_plugin](https://buf.build/grpc/ruby) |
| ✅ | [`grpc/swift`](generators/grpc/swift/README.md) | v1.27.6 | [protoc-gen-grpc-swift](https://buf.build/grpc/swift) |
| ⚠️ | [`grpc/swift-protobuf`](generators/grpc/swift-protobuf/README.md) | v2.5.0 | [protoc-gen-grpc-swift-2](https://buf.build/grpc/swift-protobuf) |
| ✅ | [`grpc/web`](generators/grpc/web/README.md) | v2.1.1 | [protoc-gen-grpc-web](https://buf.build/grpc/web) |
| ⚠️ | [`protocolbuffers/cpp`](generators/protocolbuffers/cpp/README.md) | v36.2 | [protoc --cpp_out](https://buf.build/protocolbuffers/cpp) |
| ✅ | [`protocolbuffers/csharp`](generators/protocolbuffers/csharp/README.md) | v36.2 | [protoc --csharp_out](https://buf.build/protocolbuffers/csharp) |
| ✳️ | [`protocolbuffers/dart`](generators/protocolbuffers/dart/README.md) | v25.1.0 | [protoc-gen-dart](https://buf.build/protocolbuffers/dart) |
| ✅ | [`protocolbuffers/go`](generators/protocolbuffers/go/README.md) | v1.36.12 | [protoc-gen-go](https://buf.build/protocolbuffers/go) |
| ⚠️ | [`protocolbuffers/java`](generators/protocolbuffers/java/README.md) | v36.2 | [protoc --java_out](https://buf.build/protocolbuffers/java) |
| ✅ | [`protocolbuffers/js`](generators/protocolbuffers/js/README.md) | v4.0.3 | [protoc-gen-js](https://buf.build/protocolbuffers/js) |
| ✅ | [`protocolbuffers/kotlin`](generators/protocolbuffers/kotlin/README.md) | v36.2 | [protoc --kotlin_out](https://buf.build/protocolbuffers/kotlin) |
| ⚠️ | [`protocolbuffers/objc`](generators/protocolbuffers/objc/README.md) | v36.2 | [protoc --objc_out](https://buf.build/protocolbuffers/objc) |
| ⚠️ | [`protocolbuffers/php`](generators/protocolbuffers/php/README.md) | v36.2 | [protoc --php_out](https://buf.build/protocolbuffers/php) |
| ✅ | [`protocolbuffers/pyi`](generators/protocolbuffers/pyi/README.md) | v36.2 | [protoc --pyi_out](https://buf.build/protocolbuffers/pyi) |
| ✅ | [`protocolbuffers/python`](generators/protocolbuffers/python/README.md) | v36.2 | [protoc --python_out](https://buf.build/protocolbuffers/python) |
| ✅ | [`protocolbuffers/ruby`](generators/protocolbuffers/ruby/README.md) | v36.2 | [protoc --ruby_out](https://buf.build/protocolbuffers/ruby) |

## Install

Download the binary for your platform from
[Releases](https://github.com/exa-pub/protoc-gen-uni/releases)
(`protoc-gen-uni_<version>_{linux_amd64,linux_arm64,darwin_amd64,darwin_arm64,windows_amd64.exe}`,
sha256 in `checksums.txt`), rename it to `protoc-gen-uni` (`protoc-gen-uni.exe`), make it executable
and put it on your `PATH`. Linux binaries are fully static (musl) and run on any distribution.

The image `ghcr.io/exa-pub/protoc-gen-uni` (`FROM scratch`, `linux/amd64` and `linux/arm64`, tags
`<version>` and `latest`) holds only the binary, as `/protoc-gen-uni`.

## Building from source

C11, CMake ≥ 3.28 and Ninja; release binaries are built in a Docker toolchain image:

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
UBSan; 2 minutes of fuzzing) → `docker` (image `FROM scratch` for linux/amd64 and linux/arm64, pushed
from `main`) → `release`.

- **version** — the target version is fixed at the start of the run: latest `vX.Y.Z` tag + bump based on
  [conventional commits](https://www.conventionalcommits.org/) (`feat!:` / `BREAKING CHANGE:` → major,
  minor before 1.0; `feat:` → minor; anything else → patch). No tags yet → `v0.1.0`. Pull requests get
  `vX.Y.Z-g<sha>`.
- **release** — push to `main` only: the tested binaries for linux/darwin/windows plus
  `checksums.txt`, a `vX.Y.Z` tag and a GitHub Release with generated notes.

The version is derived from commit subjects on `main`, so squash-merge pull requests with a
conventional-commit title.
