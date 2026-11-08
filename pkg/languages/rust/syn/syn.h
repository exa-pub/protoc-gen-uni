/* syn 2.0.109 (https://github.com/dtolnay/syn, MIT OR Apache-2.0, Copyright David Tolnay): the
 * parsing of Rust text that comes from plugin options, into the syn nodes of prettyplease.h.
 * Generated code itself is built as nodes (docs/LAYOUT.md); only user text goes through here.
 *
 * The subset is what such options hold (README.md): outer attributes, and types that are paths
 * with generic arguments, tuples, references, slices, arrays and `!`. Text outside the subset fails
 * with "unsupported by protoc-gen-uni: ...", text that does not lex with the panic of upstream's
 * `unwrap()`. */
#ifndef SYN_H
#define SYN_H

#include "prettyplease.h"

/* `syn::parse_str::<syn::DeriveInput>(&format!("{attr}\nstruct fake;")).unwrap().attrs`: appends the
 * outer attributes of attr (PP_ATTR nodes) to *attrs. */
void syn_parse_attrs_of_fake_struct(Str attr, Vec *attrs);

/* The type that `src` gives where quote! interpolates it as a type: `src.parse::<TokenStream>()
 * .unwrap()` (path_only false) or `syn::parse_str::<syn::Path>(src).unwrap()` (path_only true). */
PpNode *syn_parse_type(Str src, bool path_only);

#endif
