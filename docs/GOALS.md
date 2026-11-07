# Goals

protoc-gen-uni is a universal protobuf code generator: one protoc plugin that produces exactly what
the plugins on [buf.build](https://buf.build/plugins) produce, for any plugin and any of its
versions, without their runtimes.

## Why

A project that generates code for several languages needs protoc plugins written in Go, Java,
Node.js, Python, Rust, Swift and Dart, each with its own toolchain, runtime and version pinning.
buf.build hides that behind remote plugins, which needs the network and a third-party service.
protoc-gen-uni replaces all of them with one file that runs locally, offline, in any build system.

## Goals

- **Exact.** The output of `uni-name`/`uni-version` is byte for byte that of the upstream plugin at
  that version, including its bugs, error texts and option parsing. Every change is checked against
  checksums of the upstream output.
- **Universal.** Every plugin of the buf.build protobuf, connect and grpc collections, and the
  versions people pin, not only the latest. No plugin is special: every port follows the same
  rules and layout ([RULES.md](RULES.md), [LAYOUT.md](LAYOUT.md)).
- **Self-contained.** One statically linked binary that depends only on the C library: Linux
  x86_64 and aarch64 (musl), macOS, Windows. No runtimes, no network, no files besides the request.
- **Small.** Size is a feature: the binary stays a small fraction of what the upstream plugins
  weigh together. Code shared by plugins and versions exists once; a new version costs its
  difference from the previous one, not a copy. Size is measured per generator.
- **Fast.** A request takes milliseconds, not the startup time of a JVM or Node.js.
- **Robust.** Works on real projects (large trees, editions, unusual names) and on hostile input:
  bad input gives an error, never a crash, a hang or unbounded memory.

## Non-goals

- New features, options or fixes beyond upstream: a different output is a bug.
- Running or downloading the upstream plugins.
- Generating code from anything but a `CodeGeneratorRequest`.
