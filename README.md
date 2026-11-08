# protoc-gen-uni

The protobuf plugins of [buf.build](https://buf.build/plugins) in one offline binary.

protoc-gen-uni is a protoc plugin written in C that contains ports of the buf.build plugins. Each
port produces byte for byte what the upstream plugin produces at that version, so one static
≈3 MB file replaces remote plugins and the Go, Node.js, Python, Rust, JVM, Swift and Dart
toolchains that the plugins need locally. It needs no network and no runtime.

## Usage

Replace a remote plugin with `protoc-gen-uni` and pass the plugin's buf.build name and version in
`uni-name` and `uni-version`. All other options go to the plugin unchanged.

```yaml
# buf.gen.yaml
version: v2
plugins:
  # was: - remote: buf.build/bufbuild/es:v2.17.0
  - local: protoc-gen-uni
    out: gen/ts
    opt: [uni-name=bufbuild/es, uni-version=v2.17.0, target=ts]
```

With protoc: `protoc --uni_out=gen/ts --uni_opt=uni-name=bufbuild/es,uni-version=v2.17.0,target=ts a.proto`.
With Docker: `local: [docker, run, --rm, -i, ghcr.io/exa-pub/protoc-gen-uni:latest]`.

`protoc-gen-uni --list` prints the plugins and versions it contains.

## Plugins

- ✅ the same output as upstream, with every option.
- ✳️ the same, including the formatter upstream runs on its output.
- ⚠️ some options or inputs are not supported; they fail with an error that names them.

The README of each plugin lists what it does not support and how it differs.

| | uni-name | version |
|:-:|----------|---------|
| ⚠️ | [`anthropics/buffa`](generators/anthropics/buffa/README.md) | v0.9.2 |
| ✅ | [`apple/swift`](generators/apple/swift/README.md) | v1.39.0 |
| ✅ | [`bufbuild/es`](generators/bufbuild/es/README.md) | v2.17.0 |
| ✅ | [`bufbuild/grpc-py`](generators/bufbuild/grpc-py/README.md) | v0.5.0 |
| ✅ | [`bufbuild/py`](generators/bufbuild/py/README.md) | v0.7.0 |
| ⚠️ | [`community/danielgtaylor-betterproto`](generators/community/danielgtaylor-betterproto/README.md) | v1.2.5 |
| ✅ | [`community/google-gnostic-openapi`](generators/community/google-gnostic-openapi/README.md) | v0.7.1 |
| ✅ | [`community/mfridman-go-json`](generators/community/mfridman-go-json/README.md) | v1.6.0 |
| ⚠️ | [`community/nanopb`](generators/community/nanopb/README.md) | v0.4.91 |
| ✅ | [`community/neoeinstein-prost`](generators/community/neoeinstein-prost/README.md) | v0.5.0 |
| ⚠️ | [`community/neoeinstein-prost-crate`](generators/community/neoeinstein-prost-crate/README.md) | v0.5.0 |
| ✅ | [`community/neoeinstein-prost-serde`](generators/community/neoeinstein-prost-serde/README.md) | v0.4.0 |
| ⚠️ | [`community/neoeinstein-tonic`](generators/community/neoeinstein-tonic/README.md) | v0.5.0 |
| ⚠️ | [`community/nipunn1313-mypy`](generators/community/nipunn1313-mypy/README.md) | v5.1.0 |
| ⚠️ | [`community/nipunn1313-mypy-grpc`](generators/community/nipunn1313-mypy-grpc/README.md) | v5.1.0 |
| ✅ | [`community/planetscale-vtprotobuf`](generators/community/planetscale-vtprotobuf/README.md) | v0.6.0 |
| ✅ | [`community/protobuf-c`](generators/community/protobuf-c/README.md) | v1.5.2 |
| ✅ | [`community/roadrunner-server-php-grpc`](generators/community/roadrunner-server-php-grpc/README.md) | v6.0.0 |
| ⚠️ | [`community/scalapb-scala`](generators/community/scalapb-scala/README.md) | v0.11.21 |
| ⚠️ | [`community/scalapb-zio-grpc`](generators/community/scalapb-zio-grpc/README.md) | v0.6.4 |
| ⚠️ | [`community/stephenh-ts-proto`](generators/community/stephenh-ts-proto/README.md) | v2.18.1 |
| ⚠️ | [`community/timostamm-protobuf-ts`](generators/community/timostamm-protobuf-ts/README.md) | v2.11.1 |
| ✅ | [`connectrpc/dart`](generators/connectrpc/dart/README.md) | v2.0.1 |
| ✅ | [`connectrpc/go`](generators/connectrpc/go/README.md) | v2.0.2 |
| ✅ | [`connectrpc/gosimple`](generators/connectrpc/gosimple/README.md) | v1.21.1 |
| ✳️ | [`connectrpc/kotlin`](generators/connectrpc/kotlin/README.md) | v0.10.0 |
| ✅ | [`connectrpc/py`](generators/connectrpc/py/README.md) | v0.13.0 |
| ✅ | [`connectrpc/query-es`](generators/connectrpc/query-es/README.md) | v2.3.1 |
| ✳️ | [`connectrpc/rust`](generators/connectrpc/rust/README.md) | v0.9.0 |
| ✅ | [`connectrpc/swift`](generators/connectrpc/swift/README.md) | v1.2.3 |
| ✅ | [`connectrpc/swift-mocks`](generators/connectrpc/swift-mocks/README.md) | v1.2.3 |
| ✅ | [`grpc-ecosystem/gateway`](generators/grpc-ecosystem/gateway/README.md) | v2.33.0 |
| ⚠️ | [`grpc-ecosystem/openapiv2`](generators/grpc-ecosystem/openapiv2/README.md) | v2.33.0 |
| ✅ | [`grpc/cpp`](generators/grpc/cpp/README.md) | v1.85.1 |
| ✅ | [`grpc/csharp`](generators/grpc/csharp/README.md) | v1.85.1 |
| ✅ | [`grpc/go`](generators/grpc/go/README.md) | v1.6.2 |
| ✅ | [`grpc/java`](generators/grpc/java/README.md) | v1.85.1 |
| ✳️ | [`grpc/kotlin`](generators/grpc/kotlin/README.md) | v1.5.0 |
| ✅ | [`grpc/node`](generators/grpc/node/README.md) | v1.13.1 |
| ✅ | [`grpc/objc`](generators/grpc/objc/README.md) | v1.85.1 |
| ✅ | [`grpc/php`](generators/grpc/php/README.md) | v1.85.1 |
| ✅ | [`grpc/python`](generators/grpc/python/README.md) | v1.85.1 |
| ✅ | [`grpc/ruby`](generators/grpc/ruby/README.md) | v1.85.1 |
| ✅ | [`grpc/swift`](generators/grpc/swift/README.md) | v1.27.6 |
| ⚠️ | [`grpc/swift-protobuf`](generators/grpc/swift-protobuf/README.md) | v2.5.0 |
| ✅ | [`grpc/web`](generators/grpc/web/README.md) | v2.1.1 |
| ⚠️ | [`protocolbuffers/cpp`](generators/protocolbuffers/cpp/README.md) | v36.2 |
| ✅ | [`protocolbuffers/csharp`](generators/protocolbuffers/csharp/README.md) | v36.2 |
| ✳️ | [`protocolbuffers/dart`](generators/protocolbuffers/dart/README.md) | v25.1.0 |
| ✅ | [`protocolbuffers/go`](generators/protocolbuffers/go/README.md) | v1.36.12 |
| ⚠️ | [`protocolbuffers/java`](generators/protocolbuffers/java/README.md) | v36.2 |
| ✅ | [`protocolbuffers/js`](generators/protocolbuffers/js/README.md) | v4.0.3 |
| ✅ | [`protocolbuffers/kotlin`](generators/protocolbuffers/kotlin/README.md) | v36.2 |
| ⚠️ | [`protocolbuffers/objc`](generators/protocolbuffers/objc/README.md) | v36.2 |
| ⚠️ | [`protocolbuffers/php`](generators/protocolbuffers/php/README.md) | v36.2 |
| ✅ | [`protocolbuffers/pyi`](generators/protocolbuffers/pyi/README.md) | v36.2 |
| ✅ | [`protocolbuffers/python`](generators/protocolbuffers/python/README.md) | v36.2 |
| ✅ | [`protocolbuffers/ruby`](generators/protocolbuffers/ruby/README.md) | v36.2 |

## Install

Download the binary for your platform from
[Releases](https://github.com/exa-pub/protoc-gen-uni/releases), rename it to `protoc-gen-uni`
(`protoc-gen-uni.exe` on Windows) and put it on your `PATH`. Linux binaries are static and run on
any distribution. The image `ghcr.io/exa-pub/protoc-gen-uni` (`linux/amd64`, `linux/arm64`) holds
only the binary.

## Testing

Every change is tested against checksums of the buf.build plugins' output, on test inputs and on
real projects (googleapis, OpenTelemetry, protovalidate), on Linux, macOS and Windows, with
sanitizers and fuzzing.

## Contributing

[CONTRIBUTING.md](CONTRIBUTING.md) covers building, tests and adding plugins and versions;
[docs/GOALS.md](docs/GOALS.md) explains what the project is for.

## License

MIT. Ported code keeps the license of its upstream plugin (see the plugin's README).
