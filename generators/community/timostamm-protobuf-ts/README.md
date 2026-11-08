# community/timostamm-protobuf-ts

C port of `protoc-gen-ts` from [protobuf-ts](https://github.com/timostamm/protobuf-ts)
(`@protobuf-ts/plugin`, Apache-2.0): <https://buf.build/community/timostamm-protobuf-ts>. No
Node.js required. It generates TypeScript: message interfaces and `MessageType` classes with
reflection information and speed-optimized methods (`<name>.ts`), service types, generic and
`@grpc/grpc-js` clients (`<name>.client.ts`, `<name>.grpc-client.ts`) and servers
(`<name>.server.ts`, `<name>.grpc-server.ts`).

```yaml
plugins:
  - local: protoc-gen-uni
    out: gen/ts
    opt: [uni-name=community/timostamm-protobuf-ts, uni-version=v2.11.1, long_type_string]
```

Options are the same as upstream: [MANUAL.md](https://github.com/timostamm/protobuf-ts/blob/v2.11.1/MANUAL.md)
and `packages/plugin/src/options.ts`.

## Versions

| version | upstream sources | libraries |
|---------|------------------|-----------|
| v2.11.1 | [`packages/plugin/src`](https://github.com/timostamm/protobuf-ts/tree/v2.11.1/packages/plugin/src) | TypeScript 3.9.10 printer, @bufbuild/protobuf and @bufbuild/protoplugin 2.5.2, @protobuf-ts/runtime 2.11.1 |

Each `.c` file names the upstream file it ports. The libraries are in `pkg/languages/ecmascript`:

- [`formatters/typescript`](../../../pkg/languages/ecmascript/formatters/typescript/README.md): the
  TypeScript 3.9.10 node factory and printer (`ts.createPrinter().printFile`). The buf.build
  plugin bundles the plugin with esbuild and ships TypeScript 3.9.10 next to it.
- [`protobuf`](../../../pkg/languages/ecmascript/protobuf/protobuf.h) and
  [`protoplugin`](../../../pkg/languages/ecmascript/protoplugin/protoplugin.h): the registry,
  descriptors, comments and declaration strings. They port 2.17.0; the parts that differ in 2.5.2
  are switched off for this plugin: no name validation in `createFileRegistry`
  (`registry_create_unchecked`) and comments without line-break normalization
  (`comments_normalize_line_breaks`).
- [`protobuf-ts-runtime`](../../../pkg/languages/ecmascript/protobuf-ts-runtime/runtime.h): the parts
  of `@protobuf-ts/runtime` the plugin runs at generation time: reflection info, `create()`
  defaults, the binary reader and JSON writer with which custom options become JSON, enum objects.
- [`std`](../../../pkg/languages/ecmascript/std/es_std.h): `Number#toString` and the property
  order of JavaScript objects.

**Class: formatted** ([LAYOUT.md](../../../docs/LAYOUT.md)). protoc-gen-ts builds TypeScript syntax
trees with the TypeScript 3.9 factory and prints them; the port builds the same nodes in the same
order (the order of the calls matters: imports are added at the top of the file, so they are
listed in reverse order of first use) and prints them with the printer port. The methods of
well-known types and `google.type` messages that upstream writes as text and parses
(`typescriptMethodFromText`) are decoded from their parsed trees
(`well_known_types_trees.c`, see its header), with the imported names as parameters.

All options are implemented except the JavaScript outputs (below). `ts.exclude_options`,
`ts.client` and `ts.server` of `protobuf-ts.proto` are read from the descriptors as upstream does.

## Known differences

- Errors: `ParameterError` (options) and `PluginMessageError` (invalid `ts.client` / `ts.server`)
  are byte-exact. Where upstream fails with a JavaScript error, its response holds the stack trace
  (with paths of the bundle); the port reports the message, prefixed with the error class
  (`Error: `, `TypeError: `, `RangeError: `). Examples: a malformed custom option value, an enum
  value named `__proto__` ("not a typescript enum object"), a message whose full name is a member
  of the plugin's `WellKnownTypes` class or of `Object.prototype` (`toString`, `options`, ...).
- A request that cannot be parsed: like upstream, the plugin exits with status 1 and a message on
  stderr (`Plugin failed to read CodeGeneratorRequest ...`), without the stack trace.
- Not supported (generation fails with `unsupported by protoc-gen-uni: ...`):
  - `output_javascript`, `output_javascript_es2015` ... `output_javascript_es2020`: upstream
    transpiles the TypeScript output with the TypeScript compiler (`program.emit` with
    declarations), which is not ported. `output_legacy_commonjs` alone has no effect, as upstream;
  - names of messages, fields, oneofs, enums, enum values, services, methods, extensions and
    package components that are not identifiers (`[A-Za-z_][A-Za-z0-9_]*`). @bufbuild/protobuf
    2.5.2 does not validate names; protoc and buf never write other names;
  - custom option values nested deeper than 100 messages or groups, and custom option message
    types whose own options use custom option message types nested deeper than 200 levels.
    Upstream recurses on the JavaScript stack, whose limit depends on Node.js and its frame
    sizes (depth 150 generates fine there); the port gives up at a fixed depth.
- Custom options of a request with two extensions of the same full name (protoc and buf reject
  it): the options are read with the last one only.
