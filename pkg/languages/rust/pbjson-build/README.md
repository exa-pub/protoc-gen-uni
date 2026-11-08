# pbjson-build 0.8.0, C port

The code generator of [pbjson](https://github.com/influxdata/pbjson/tree/3587e007496f87790afd7ac7b5ff45cfe1df3297/pbjson-build/src)
(MIT, Copyright (c) 2020 InfluxData): `serde::Serialize` and `serde::Deserialize` implementations
that follow the protobuf JSON mapping for the types prost-build generates. Used by
protoc-gen-prost-serde (`generators/community/neoeinstein-prost-serde`). API: `pbjson_build.h`;
unity build `pbjson_build.c`. It writes text with `write!`/`writeln!` and nothing is
pretty-printed, so the port prints the same text.

| upstream file (src/) | C file |
|---|---|
| `escape.rs` | `escape.c` |
| `descriptor.rs` | `descriptor.c` |
| `resolver.rs` | `resolver.c` |
| `message.rs` | `message.c` |
| `generator.rs` (`Indent`, shared impl blocks) | `codegen.c` |
| `generator/enumeration.rs` | `codegen_enumeration.c` |
| `generator/message.rs` | `codegen_message.c` |
| `lib.rs` (`Builder` without `build()` and its file output) | `builder.c` |

`unsupported.c` is not a port: a non-ASCII identifier that goes through heck case conversion or
keyword escaping fails with `unsupported by protoc-gen-uni: ...` (upstream uses Unicode case
mapping, [`heck`](../heck) only ASCII).

The descriptor set is a `BTreeMap` that upstream searches linearly for every field type, copying
the package into every type path. The port registers the types in insertion order with a hash
index for duplicates, sorts them once (in `pbjson_builder_generate`), shares the package of a type
path and finds a type by binary search per package length, with the same order, results and
panics. Upstream panics become `fail()`; upstream recurses without a limit through map entries
whose value is a map entry, the port fails at depth 64.

Unit tests: `tests/pbjson_build_test.c` (upstream's tests of `descriptor.rs` and `resolver.rs`).
