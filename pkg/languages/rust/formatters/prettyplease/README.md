# prettyplease 0.2.37, C port

The Rust pretty printer that the Rust code generators run over the code they build:
`prettyplease::unparse(&syn::parse2::<syn::File>(tokens)?)` in buffa-codegen 0.9.2
(`lib.rs` `format_tokens`), tonic-build 0.14.2 and connectrpc-codegen 0.9.0. Versions are those
locked by `protoc-gen-buffa 0.9.2` (`Cargo.lock`): prettyplease 0.2.37 with default features (no
`verbatim`), syn 2.0.114, proc-macro2 1.0.106. protoc-gen-tonic 0.5.0 locks syn 2.0.109 and
proc-macro2 1.0.103, with the same layout for the trees it builds (community/neoeinstein-tonic
goldens). Upstream:
<https://github.com/dtolnay/prettyplease/tree/0.2.37> (MIT OR Apache-2.0, Copyright David Tolnay;
`src/algorithm.rs` is adapted from rustc_ast_pretty `pp.rs`, The Rust Project Developers).

## Layout

Formatter library (docs/LAYOUT.md): no token stream, no syn parser. A generator builds the syn
nodes directly with the constructors of `prettyplease.h` and `pp_unparse` prints them.

- `algorithm.c`: `algorithm.rs` (the Oppen printer, `MARGIN` 89, `INDENT` 4, `MIN_SPACE` 60),
  `ring.rs` and `convenience.rs`, exact.
- `attr.c`, `mac.c` (with `token.rs`), `path.c` (with `lifetime.rs`, `lit.rs`), `generics.c`,
  `ty.c` (with `data.rs`), `pat.c`, `expr.c` (with `classify.rs`), `stmt.c`, `item.c` (with
  `file.rs`): the printing rules of each syn node kind, same function names (`p_` prefix) and
  order of printer calls.
- `nodes.c`: the node constructors, and `pp_tt_push_src`, a lexer of constant macro and attribute
  bodies into token trees as rustc lexes them for `quote!` (maximal-munch punctuation with
  `Joint` spacing); input-derived identifiers and literals are pushed as their own tokens. `PpNode` is one struct for every syn node (`prettyplease.h`
  lists the fields per kind). Literals and identifiers carry their token text (`r#type`,
  `"a\n"`, `1u32`, `1.5f32`); the generator produces it as proc-macro2 would.
- `prettyplease.c`: unity build; conventions in `internal.h`.

## Building trees as syn parses them

prettyplease prints a syn tree; the tree a generator builds must be the one syn parses from the
tokens upstream quotes, because layout depends on the node kinds:

- Precedence comes from the tokens: `quote!(#a as u64)` with `a = x + y` is `x + (y as u64)` for
  syn. Parentheses in the tokens are `ExprParen` nodes (`pp_e_paren`).
- `src/fixup.rs` and `src/precedence.rs` are not ported: they insert parentheses that a tree
  built by hand may lack, which never happens for a parsed tree, so every `needs_paren` /
  `needs_group` of prettyplease is false here.
- Statements: `pp_s_expr` applies syn's rule (`foo!(..);` and `foo! { .. }` are `Stmt::Macro`).
  A trailing expression without `;` is `Stmt::Expr(e, None)`.
- A negative literal created by `Literal::i32_suffixed(-3)` is one literal token (`-3i32`, an
  `ExprLit`), while `-3i32` in quoted source is `ExprUnary(Neg, 3i32)`; they lay out differently
  in arrays (`simple_array`).
- Macro bodies are token trees (`pp_macro`, `pp_tt_*`), printed by `macro_rules_tokens`; the
  spacing (`Joint`/`Alone`) of punctuation matters as in proc-macro2.

`prefix_subexpr` (the receivers of `.await`, calls, fields, indexing, method calls and `?`) is a
loop over the chain instead of a recursion, with the same output: a generated method chain is as
long as its input (one route per method in connectrpc-codegen's router).

Precise capturing bounds (`impl Trait + use<'a, Self>`, connectrpc-codegen) are
`PP_PRECISE_CAPTURE` nodes (`pp_precise_capture`).

Not ported (generated code has none; they fail): `macro_rules!` definitions, `Verbatim` nodes,
foreign items, unions, trait aliases, `&raw`, `yield`, `try` blocks, bare function lifetimes and
ABIs, restricted visibility with a path (`pub(in a::b)`).

`attr_tokens` (attribute token lists) prints at most 255 nested groups and beyond fails with
`unsupported by protoc-gen-uni: attribute tokens nested more than 255 levels deep`. Upstream has no
limit, but its output grows with the square of the depth (every level is an indented line once
the list breaks), so an attribute of a few hundred kilobytes would print gigabytes.

## Tests

`tests/prettyplease_test.c` (`unit/prettyplease_test`): node fixtures whose expected text is the
output of prettyplease 0.2.37 for the same source (`prettyplease::unparse(&syn::parse_file(src))`
with syn 2.0.114), including pieces of the upstream anthropics/buffa v0.9.2 output (a fixed
point). The generator goldens exercise the rest.
