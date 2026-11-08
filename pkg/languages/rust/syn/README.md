# syn 2.0.109 subset, C port

The parsing of Rust text that comes from plugin options: `syn::parse_str` and `str::parse::<
TokenStream>` on attribute and type texts, as protoc-gen-tonic 0.5.0 and tonic-build 0.14.2 run
them (<https://github.com/dtolnay/syn/tree/2.0.109>, MIT OR Apache-2.0, Copyright David Tolnay).
Generated code is never parsed: generators build the syn nodes of
[`prettyplease.h`](../formatters/prettyplease/prettyplease.h) directly (docs/LAYOUT.md). Text is
lexed by the proc-macro2 1.0.103 fallback lexer ([`../proc-macro2/parse.c`](../proc-macro2/parse.c),
`src/parse.rs`, exact for ASCII identifiers; a non-ASCII identifier char is unsupported).

`syn.c` ports the parts it needs of `src/attr.rs`, `src/derive.rs`, `src/path.rs`, `src/ty.rs`,
`src/lit.rs` and `src/ident.rs`:

- `syn_parse_attrs_of_fake_struct`: the outer attributes of `"<text>\nstruct fake;"` parsed as a
  `DeriveInput` (tonic-build `generate_attributes`). An attribute's meta is a path
  (`Path::parse_mod_style`, or `unsafe`), a delimited token list (kept as tokens), or `path = value`
  where the value is a literal (`-` before a number literal included, `true`, `false`) or a path.
  Doc comments (`///`, `/** */`) are attributes as proc-macro2 lexes them. `LitStr::value` unescapes
  strings for prettyplease's doc comment printing.
- `syn_parse_type`: a type (`TokenStream` interpolated where quote! puts a type) or a `Path`. Types:
  paths with a leading `::`, `super`/`self`/`crate`/`Self` segments, raw identifiers and angle
  bracketed arguments of types and lifetimes (also with `::<`); `()` and tuples; parenthesized
  types; references with lifetime and `mut`; `*const`/`*mut` pointers; slices; arrays whose length
  is an integer literal; `!`. Paths do not take parenthesized arguments.

Everything else, valid Rust or not (qualified paths, `dyn`/`impl` types, associated type bindings,
expressions other than literals and paths, inner attributes, text after the attributes), fails
with `unsupported by protoc-gen-uni: Rust attribute|type "<text>" ...`. A lexer error fails with
the panic text of upstream's `unwrap()` (`Error("lex error")`, `LexError { span: Span }`). Nesting
of types is limited to 64 levels (deeper types fail as unsupported). Token groups are lexed without
a limit, as upstream; prettyplease prints at most 255 nested groups in an attribute and fails as
unsupported beyond (upstream has no limit, but its output grows with the square of the depth).
