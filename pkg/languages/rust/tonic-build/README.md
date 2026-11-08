# tonic-build 0.14.2, C port

The generic gRPC client and server generator of [tonic](https://github.com/hyperium/tonic)
(`tonic-build/src` at commit 03894a27, MIT, Copyright (c) 2025 Lucio Franco), as
protoc-gen-tonic 0.5.0 (community/neoeinstein-tonic) drives it. Default features (`transport`).

## Layout

- `lib.c`: `src/lib.rs` (`Attributes`, `format_service_name`, `format_method_path`,
  `generate_attributes`, `generate_doc_comment(s)`, `match_name`, `naive_snake_case`) and
  `src/code_gen.rs` (`CodeGenBuilder` defaults).
- `client.c`, `server.c`: `src/client.rs`, `src/server.rs`. Each `quote!` site is built as the syn
  nodes (`prettyplease.h`) that `syn::parse2::<syn::File>` gives for its tokens, in the same order;
  the caller prints the items with `pp_unparse` (formatter library, docs/LAYOUT.md).
- `internal.h`: shorthands for those nodes; `tonic_build.c`: unity build.
- The `Service` and `Method` traits are the structs `TonicService` and `TonicMethod`;
  `request_response_name` is a callback, called where upstream calls it.
- `generate_attributes` parses the attribute texts with `syn::parse_str::<DeriveInput>`, ported
  in [`../syn`](../syn) for the subset listed there.
- `format_ident!` checks identifiers like proc-macro2 (`Ident::new`, `Ident::new_raw`) and fails
  with its panic text.

Not ported (no generator sets them): `CodeGenBuilder::disable_comments`, `use_arc_self`,
`generate_default_stubs`, `Method::deprecated` other than `false`, and `src/manual.rs` (services
defined in Rust instead of protobuf).

## Tests

`tests/tonic_build_test.c` (`unit/tonic_build_test`): the upstream tests of `src/lib.rs`, and the
syn parsing of option texts printed with prettyplease. The generator goldens cover the rest.
