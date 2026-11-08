# buffa-codegen 0.9.2, C port

The code generator library of [buffa](https://github.com/anthropics/buffa/tree/v0.9.2/buffa-codegen)
(Apache-2.0, Copyright Anthropic, PBC), used by protoc-gen-buffa (`generators/anthropics/buffa`)
and by connectrpc-codegen (connectrpc/rust, buffa-codegen 0.9.1). API: `buffa_codegen.h`; unity
build `buffa_codegen.c`.

connectrpc-codegen uses only `CodeGenContext::for_generate`, `rust_type_relative(_split)`,
`make_field_ident`, `rust_path_to_tokens`, `proto_path_to_stem`, `package_to_(mod_)filename`,
`FeatureGateNames::is_valid_name` and `decode_request` (the `bc_*` functions of that section of
the header). Their code is the same in 0.9.1 and 0.9.2 (the 0.9.2 changes to `context.rs` only
share corpus state through `Arc`s), so nothing is versioned.

`bc_decode_request` checks the element-memory budget of `decode_request` (the option, else
1 GiB; the `BUFFA_ELEMENT_MEMORY_LIMIT` environment variable is not read, as for the remote
plugins): every element of a repeated message, string or bytes field of the descriptor types
costs `size_of` of its Rust type (buffa-descriptor 0.9.1 and 0.9.2, 64-bit, measured with rustc),
walked on the raw request (`lib.c`).

Output: buffa-codegen builds `quote!` token streams, parses them with syn and prints them with
prettyplease. The port builds the syn nodes with the prettyplease port's constructors
(`pkg/languages/rust/formatters/prettyplease/prettyplease.h`), as syn would parse the quoted
tokens (see that README: precedence from tokens, `Stmt::Macro`, negative literals), and literal
text with `pkg/languages/rust/proc-macro2`.

## Port status

Every upstream file is ported except the buffa-build-only configuration (custom string/bytes/map/repeated/pointer
representations, custom attributes, `SharedCorpusContext`, `shared_descriptor_pool_root`), which
the protoc plugin cannot set: the defaults apply. Warnings are not collected.

| upstream file (src/) | C file |
|---|---|
| `lib.rs`: CodeGenConfig, decode options, exclude_package, configuration checks | `lib.c` |
| `lib.rs`: generate_with_diagnostics, generate_package, generate_proto_content, generate_package_mod, format_tokens, file names, validate_file, validate_shared_root_name | `generate.c` |
| `context.rs`, `features.rs` (+ buffa-descriptor `features.rs`), `idents.rs`, `field_names.rs`, `imports.rs`, `effective_extern_paths`, oneof/pointer resolution of `oneof.rs` | `context.c` |
| `feature_gates.rs` (cfg helpers; FeatureGates in `lib.c`) | `feature_gates.c` |
| `feature_overrides.rs` | `feature_overrides.c` |
| `comments.rs` | `comments.c` |
| `defaults.rs` | `defaults.c` |
| `enumeration.rs` | `enumeration.c` |
| `message.rs` (structs, Debug, setters, Default, re-exports, registry consts) | `message.c` |
| `message.rs`/`oneof.rs`/`view.rs` JSON (serde attributes, custom Deserialize, Serialize impls) | `json.c` |
| `impl_message.rs` | `impl_message.c` (view encoding in `view.c`) |
| `oneof.rs` | `oneof.c` |
| `extension.rs` | `extension.c` |
| `view.rs`, `owned_view.rs` | `view.c` |
| `lazy_view.rs` | `lazy_view.c` (lazy encode statements in `view.c`) |
| `impl_text.rs` | `impl_text.c` |
| `reflect.rs`, `reflect_owned.rs`, `reflect_view.rs` | `reflect.c` |
| (quote!/proc-macro2 glue) | `quote.c` |

Descriptors are the pkg/pb messages of the request, read like buffa reads its descriptor structs.
Token streams are `Vec`s of syn nodes of the kind the quoting site produces. The embedded
FileDescriptorSet is the request's files re-encoded by pkg/pb without `source_code_info` (known
fields in number order, unknown fields last, as buffa encodes).
