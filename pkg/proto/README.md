# pkg/proto: the shared descriptor model

The descriptor model that plugin libraries build on (docs/LAYOUT.md). It covers what
descriptor.proto and the editions spec define, which every protobuf runtime computes the same way.
It follows protoc's model (`descriptor.h`, protobuf v36.2), the reference implementation. The API
is `proto.h` (prefix `proto_`/`Proto`).

The model holds no validation, error texts, supported editions, edition defaults, edition names,
name resolution rules or derived names. Those differ between runtimes and stay in each library.
A library keeps extra data per descriptor behind `user`.

## API

| file | what | upstream |
|---|---|---|
| `desc.c` | `ProtoFile`, `ProtoMessage`, `ProtoField`, `ProtoOneof`, `ProtoEnum`, `ProtoEnumValue`, `ProtoService`, `ProtoMethod`, `ProtoExtensionRange`. They share a common head (`ProtoDesc`: kind, index, name, full name, options, the features set on the element and the resolved ones, the proto) and are linked by pointers: containing types, extension scopes, extendees, message and enum types, method input and output. The predicates are `proto_field_has_presence`, `_is_packed`, `_is_required`, `_is_map`, `_requires_utf8_validation`, `_has_optional_keyword`, `_real_oneof`, `proto_oneof_is_synthetic`, `proto_enum_is_closed`, `proto_message_is_map_entry`, plus lookups by number and name. | descriptor.h |
| `pool.c` | `ProtoPool`: files by name, symbols by full name (packages included), extensions by extendee and number, and the extensions of a message in the order they were added. | DescriptorPool::Tables |
| `features.c` | `proto_edition_features` (GetEditionFeatureSetDefaults over a FeatureSetDefaults message, the library's data), `proto_merge_features` (MergeFeatures), `proto_infer_legacy_field_features` (proto2/proto3 rules), `proto_field_parent_features`, and `proto_parse_feature_set_defaults` for the bytes a runtime embeds. | feature_resolver.cc, descriptor.cc |
| `message.c` | `proto_msg_merge` (MergeFrom), `_clone`, `_clear`, setters: options and features messages over any `PbMsgDef`. Parsing and serialization stay in the library. | message.cc |
| `names.c` | `proto_json_name` (ToJsonName) | descriptor.cc |
| `source.c` | `proto_desc_source_path`, `proto_file_location` (the first location of a path), `proto_desc_location`. Comments are read from the location with the pb.h accessors. | descriptor.cc GetSourceLocation |
| `wkt.c` | `ProtoWkt`: well-known types by full name, their files, wrappers | src/google/protobuf/*.proto |

Edition defaults are data. protoc compiles them from the FeatureSet descriptor and the
generator's feature extensions (libprotoc `lp_compile_defaults`). The other runtimes embed the
bytes of `protoc --edition_defaults_out`: protobuf-go `editions_defaults.binpb`, swift-protobuf
`bundledFeatureSetDefaultBytes`, protobuf-es compiled defaults. Features are FeatureSet messages,
so extension features (`pb.cpp`, `pb.java`, `pb.go`) resolve with the core ones.

Tests: `tests/proto_test.c`. Its expected values come from protoc 36.2's own edition defaults
(`cpp_edition_defaults.h`), protobuf-go's protoc-made defaults, ToJsonName, the predicates and the
source paths of `descriptor.cc`.

## Users

- **libprotoc 36.2** (`pkg/languages/cpp/libprotoc`, protocolbuffers/ruby) builds the model.
  `LpFile`... are typedefs of the `Proto*` types and `LpPool` embeds a `ProtoPool`. The builder
  (DescriptorBuilder with its validation and errors), CompileDefaults, FeatureResolver::Create
  validation, CopyTo, the C++ type of a field and the wire format stay in libprotoc. The legacy
  `ctype` that protoc moves out of the options is kept in the field's `user`. Size with
  `-DUNI_GENERATORS=protocolbuffers/ruby` on x86_64: 111 592 B before, 111 576 B after.

## Switching the other libraries

Each library keeps its builder, because validation and its error texts are upstream behavior.
It builds `Proto*` descriptors instead of its own and drops its copies of the shared code.

- **protobuf-go protodesc** (`pkg/languages/go/protodesc`, `features`). `GoFile`... become
  `Proto*`. `GoDesc.user` moves to `user` and `GoDesc.parent` is `proto_desc_parent`. Kind and
  cardinality stay as protoreflect values computed from `type` and `label`. `GoEditionFeatures`
  are computed from the resolved FeatureSet, or stay a cached struct beside it. JSONCamelCase
  differs from ToJsonName and stays. `go_source_location_by_path`/`by_desc`/`go_desc_source_path`
  become `proto_file_location`/`proto_desc_source_path`. The registry becomes a `ProtoPool`.
  `validate.c` and the "proto: ..." errors stay.
- **protobuf-es** (`pkg/languages/ecmascript/protobuf`, `protoplugin`). `DescFile`... become
  `Proto*`, with the generated names (`n_shape`, ...) behind `user`. The `Vec` lists become
  arrays. `resolve_feature`/`feature_defaults` become the FeatureSetDefaults of @bufbuild/protobuf
  with `proto_merge_features`. `protoCamelCase` equals `proto_json_name`. Source paths, comment
  lookup, `desc_parent` and the WKT checks (`is_wrapper_desc`, `wkt_public_import_path`'s list)
  move to pkg/proto. `registry_create` keeps its error texts.
- **swift-protobuf** (`pkg/languages/swift/swift-protobuf`). `SwFile`... become `Proto*`.
  `SwFeatures` and `edition_defaults` become `bundledFeatureSetDefaultBytes` with
  `proto_edition_features`/`proto_merge_features`. The `sw_field_*` predicates are `proto_field_*`.
  The location map and path code of `comments.c` become `source.c`. `is_any` is
  `proto_wkt(full_name) == PROTO_WKT_ANY`. The fatal errors of `sw_descriptor_set` and the ASCII
  name rule stay.

## Expected size saving

Measured per function at `-Os`, x86_64, without LTO: the code each library would drop when it
uses the pkg/proto copies already linked for libprotoc. These are full-binary numbers. A binary
with only one of these generators links the shared code itself, so it gains little.

| library | dropped code | bytes |
|---|---|---:|
| protobuf-go | predicates, enum value lookups, source paths and locations, registry map | ~0.8 K |
| swift-protobuf | predicates, `resolve`, edition table, `comments.c` paths and location map | ~1.0 K |
| protobuf-es | `features.c`, source paths and location map, `desc_parent`, WKT checks | ~0.9 K |

That is about 3 KB in all. Most of each library is its builder: go `go_registry_new_file` and
`init_*` ~7 KB, es `registry_create` and `add_*` ~5 KB, swift `new_*` and `sw_descriptor_set`
~5 KB. Those stay, because their validation and errors are upstream behavior. The main gain is
for new ports: the libprotoc-based plugins (protocolbuffers/java, python, cpp, csharp, objc,
php, kotlin, pyi) and the plugins of other runtimes start from this model instead of a fourth
and fifth copy.
