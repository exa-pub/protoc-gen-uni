# dprint-typescript

Port of the formatter that ts-poet 6.12.0 runs on every file ts-proto generates:
`dprint-node` 1.0.8, which embeds **dprint-plugin-typescript 0.87.1** and **dprint-core 0.62.1**
(and swc_ecma_parser 0.139.0, which is not ported). The configuration is fixed to ts-poet's
`baseOptions` (`ts-poet/build/Code.js`) on top of dprint-node's defaults: line width 120, indent 2,
`useBraces: always`, `singleBodyPosition: nextLine`, `arrowFunction.useParentheses: force`,
`module.sortImportDeclarations: caseSensitive`, `preferSingleLine: true`, double quotes.

Upstream sources: github.com/dprint/dprint-plugin-typescript tag 0.87.1, dprint-core 0.62.1
(crates.io), dprint-swc-ext 0.12.0, swc_ecma_parser 0.139.0 (spans and comment attachment).
Licenses: MIT (dprint), Apache-2.0 (swc). Ported files keep their headers and name what they port.

## Layout (docs/LAYOUT.md, formatter without a front end)

| file | ports |
|---|---|
| `core/ir.h`, `core/print_items.c`, `core/printer.c` | dprint-core: print items, conditions, infos, the printer |
| `core/gen_separated_values.c` | dprint-core `formatting/utils/gen_separated_values` (via the plugin) |
| `ts/gen_context.c`, `ts/gen_common.c`, `ts/gen_nodes.c` | dprint-plugin-typescript `src/generation` for the node kinds ts-proto emits |
| `ts/source.c` | dprint-swc-ext `SourceTextInfo`, token and comment containers |
| `ts/ast.h`, `ts/build.c` | the node kinds (swc_ecma_ast 0.110 shapes) and the construction API (`dprint_typescript.h`) |
| `dprint_typescript.c` | the entry point, `dprint_ts_reserved_name` (swc's keyword check) and `dprint_ts_dump` |
| `ts/layout.c` | the virtual source: text, tokens, comments and swc spans of a node tree |

Generators build nodes with the API in `dprint_typescript.h` and call `dprint_ts_format_module`.
There is no TypeScript lexer or parser: the tree is what swc_ecma_parser would have produced for
the text the generator stands for (optional chains, `ExprOrSpread` arguments, `Param` wrappers,
`TsTypeAnn` from the colon, ...; the builders take care of this).

dprint's rules query source positions (line of a node, previous/next token, comments around a
node, blank lines), so `layout.c` lays the tree out into a virtual source with swc's spans and
comment attachment. The nodes carry the layout hints dprint takes from the text:

- `nl`: line break before a node (`NL_SAME`, `NL_LINE`, `NL_BLANK`). The default is ts-poet's
  layout: statements, class/interface/enum members and switch cases on their own line, everything
  else on the same line. In practice only blank lines (`NL_BLANK`) and lines around comments matter.
- comments before a node (`dts_comment`, each with its own line break) and after its last token
  (`dts_trailing_comment`), `nl_after_cm` between the last comment and the node.
- `hints`: optional tokens (`H_NO_SEMI`, `H_SEP_COMMA`, `H_TRAILING_COMMA`, `H_LEADING_OP`,
  `H_NO_PARENS`, `H_MAPPED_SEMI`) and line breaks inside a node (`H_CLOSE_NL` before the closing
  bracket, `H_OP_NL`/`H_OP2_NL` before an operator).

Checked against upstream (2026-11): all 5627 dprint calls ts-proto 2.18.1 makes for the golden
inputs and option groups, and 6865 more for 31 other option values on 6 inputs (ts-poet text
captured with a `dprint-node.format` hook, then parsed, turned into nodes with hints, laid out and
formatted) give upstream's output; the re-parsed virtual source has the same tokens, spans and
comment attachment. With only these hints kept (all other line breaks from the defaults) the result
is the same: comments with their line breaks, blank lines, the token hints, `H_CLOSE_NL` on empty
bodies (`{\n}` stays), `H_OP_NL`/`H_OP2_NL` on conditional expressions and the line breaks before
object literal properties. Generators must reproduce exactly these from their templates; examples
of why:

- `name?: string | undefined,` followed by a comment on the next line formats as a broken union
  (`has_any_node_comment_on_different_line` looks past the comma); with `;` it does not.
- `export interface Empty {\n}` keeps the line break; `{}` stays `{}`.
- a conditional written over three lines stays broken if it does not fit, collapses otherwise.
- a conditional of 117 columns in a property of a multi-line object literal: whether the
  properties start on their own lines in the source changes whether it is broken.

Other API:

- `dprint_ts_reserved_name`: whether swc would reject the code for a reserved word used as a name
  (then dprint fails and ts-poet emits the unformatted text, which the generator writes itself).
  swc's rules depend on the position: `interface` is a valid binding and type name but no
  expression, `let` a valid expression but no type, `public` a valid type unless `{` or `[`
  follows, `keyof`/`readonly`/`unique` only as an array element type. The table in
  `dprint_typescript.c` was derived from swc_ecma_parser 0.139.0 (`is_reserved_word`,
  `parse_ident`, `eat_any_ts_modifier`, `parse_ts_non_array_type`, the import specifiers) and
  checked word by word and position by position against dprint-node 1.0.8. Property names and
  the names of interfaces, type aliases, enums, namespaces and export specifiers may be anything.
- `dts_method_sig` with the key `new` makes a construct signature, as swc parses `new(...)` in a
  type member (printed `new (...)`).
- `dts_clone` (with the `user` field of nodes for the generator) and `dprint_ts_dump` (a textual
  dump of a tree with its hints, for tests and debugging).

## Tests

`tests/dprint_test.c`: dprint-core writer cases and node fixtures whose expected output comes from
upstream dprint-node 1.0.8 for the equivalent ts-poet text.

## Differences

- `deno-fmt-ignore-file` comments are not honoured (ts-proto's file header never starts with one).
- Only the node kinds ts-proto/ts-poet emit are implemented; other kinds fail with an error.
