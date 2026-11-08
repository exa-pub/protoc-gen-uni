# TypeScript 3.9.10 printer, C port

The formatting of `community/timostamm-protobuf-ts` (@protobuf-ts/plugin 2.11.1): the plugin
builds TypeScript ASTs with the TypeScript 3.9 node factory (`ts.createXxx`,
`ts.addSyntheticLeadingComment`) and prints each file with
`ts.createPrinter({newLine: LineFeed}).printFile` through a source file created from the empty
text. The buf.build remote plugin ships TypeScript **3.9.10**, so this is a port of
<https://github.com/microsoft/TypeScript/tree/v3.9.10/src/compiler> (Apache-2.0, Copyright (c)
Microsoft Corporation) for the node kinds that plugin uses. The printer has no width-based layout:
line breaks come from the node kinds, `multiLine` flags and positions only.

## Layout

Files mirror `src/compiler`: `emitter.c` (createPrinter), `factory_public.c` (factoryPublic.ts:
`ts.createXxx`, synthetic comments), `factory.c` (factory.ts: parenthesization), `utilities.c`
(utilities.ts: text writer, escaping, `getLiteralText`, `writeCommentRange`, precedence),
`scanner.c` (only `tokenToString`). `typescript.c` is a unity build; everything is `static`
except the `ts_*` API of `typescript.h` (`TsNode` with the TypeScript property names,
`TS_<SyntaxKind>` with the 3.9.10 values). Conventions: `internal.h`.

The front end (scanner, parser) is not ported. A few methods of the plugin (well-known types) are
parsed from constant text with `ts.createSourceFile` upstream; the generator builds those parsed
nodes with `ts_decode_parsed` (`decode.c`) from a compact encoding of the parsed trees: kinds,
texts, the properties the parser sets (`multiLine`, trailing commas, node flags) and the one
position relation the printer reads (a simple arrow head `x => ...`). Decoded nodes have real
(increasing) positions, so they print as parsed nodes do: the printed source file has empty text,
every position counts as one line, and literal texts are re-escaped (parsed nodes have no parent).

What the printer leaves out because protobuf-ts cannot reach it: comments and line breaks of the
source text, source maps, bundles, emit helpers, generated names, transformations (substitution,
notification), JSX and JSDoc nodes, `preserveSourceNewlines`. Unsupported node kinds stop with
`fail()`.

## Tests

`tests/printer_test.c` (`unit/typescript_printer`): trees built like protobuf-ts builds them
(`create()`, `internalBinaryRead`, a message type class, an interface with comments), with the
expected text from the upstream output; decoded parsed nodes (simple arrow heads, `multiLine`
object literals with trailing commas, back references, parameters); parenthesization and escaping
rules. The generator's golden tests cover everything else.
