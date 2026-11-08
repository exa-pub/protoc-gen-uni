# community/stephenh-ts-proto

Port of `protoc-gen-ts_proto` from [ts-proto](https://github.com/stephenh/ts-proto), as published
on buf.build (`buf.build/community/stephenh-ts-proto`). It generates TypeScript interfaces and
`MessageFns` objects (binary encode/decode with `@bufbuild/protobuf/wire`, JSON, `fromPartial`),
enums with their JSON functions, extensions, type registry and index files, and services: the
default interfaces and clients (with optional context and DataLoader batching), `@grpc/grpc-js`,
`nice-grpc`, generic definitions, `@improbable-eng/grpc-web` clients and NestJS interfaces.

```yaml
plugins:
  - local: protoc-gen-uni
    out: gen
    opt: [uni-name=community/stephenh-ts-proto, uni-version=v2.18.1, esModuleInterop=true]
```

| version | upstream |
|---|---|
| v2.18.1 | ts-proto 2.18.1 (tag `v2.18.1`, `src/*.ts`), ts-poet 6.12.0, dprint-node 1.0.8 (dprint-plugin-typescript 0.87.1, dprint-core 0.62.1), ts-proto-descriptors 2.1.0, case-anything 2.1.13 |

License: Apache-2.0 (ts-proto, ts-poet), MIT (dprint, case-anything), ISC (ts-proto-descriptors).
The buf.build image is `plugins/community/stephenh-ts-proto/v2.18.1/Dockerfile` of bufbuild/plugins
(node 26, `npm ci` with its `package-lock.json`).

## Class

Formatted: ts-proto builds code with ts-poet templates, and ts-poet formats every file with
dprint-plugin-typescript (`ts-poet/build/Code.js` `maybePretty`). The port builds the formatter's
nodes directly (no TypeScript text is parsed); the nodes carry the layout hints of ts-proto's template
text (blank lines, comments, optional `;`/`,`, `{\n}` of empty bodies, line breaks in conditional
expressions), see `pkg/languages/ecmascript/formatters/dprint-typescript/README.md`.

## Layout of the port

- `pkg/languages/ecmascript/formatters/dprint-typescript/`: the formatter (layout engine, rules,
  node-building API).
- `pkg/languages/ecmascript/ts-poet/`: ts-poet as a node library: import specs (`Import.from`),
  `def`, conditional outputs, and `Code.toString` (imports collected breadth-first over the Code tree,
  `assignAliasesIfNeeded`, `emitImports`, the prefix). A `TpCode` mirrors each ts-poet `Code` of
  ts-proto (its placeholders: imports, defs, conditional outputs, nested codes) because the
  breadth-first order decides alias numbers (`Item3`, `statusFromJSON5`) and the used helpers.
- `pkg/languages/ecmascript/ts-proto-descriptors/`: the request as ts-proto-descriptors decodes it
  (own properties only for fields on the wire: `hasOwnProperty("oneofIndex")`, `options.packed` is
  `false` when the options are present).
- `pkg/languages/ecmascript/case-anything/`: `camelCase` (gRPC method names).
- `pkg/languages/ecmascript/std/path.c`: Node.js `path` (relative import paths).
- `generators/community/stephenh-ts-proto/v2.18.1/`: `src/*.ts`: `options.c` (options.ts),
  `ctx.c` (context.ts, case.ts, sourceInfo.ts, utils.ts, visit.ts), `types.c`, `enums.c`, `utils.c`
  (main.ts `makeUtils`: the helpers), `main.c`, `main_binary.c`, `main_json.c`, `extensions.c`,
  `file.c` (main.ts, plugin.ts, generate-type-registry.ts, generate-async-iterable.ts and the index
  files of utils.ts), `struct_wrappers.c` (generate-struct-wrappers.ts), `services.c`
  (generate-services.ts, generate-grpc-js.ts, generate-nice-grpc.ts,
  generate-generic-service-definition.ts, encode.ts), `nestjs.c` (generate-nestjs.ts),
  `grpc_web.c` (generate-grpc-web.ts), `raw.c` (the unformatted text, see below).

When dprint cannot parse a file (a name that is a reserved word where swc rejects it:
`message class {}` makes `export const class: MessageFns<class>`, `message interface {}` makes
`interface.fromPartial(...)`, `message public {}` makes `(): public {`), ts-poet's `maybePretty`
returns the code as ts-proto's templates wrote it. `ts-poet` detects this on the node tree
(`dprint_ts_reserved_name`, swc's rules per position, see the dprint-typescript README) and
assembles the raw text (prefix, `emitImports` text, body);
`raw.c` writes the body: a text port of the templates of `generateFile` for what such a file may
contain (see Unsupported).

## Options

All of `src/options.ts` is parsed like upstream (`optionsFromParameter`: plain JavaScript values,
repeated keys become (nested) arrays, `true`/`false` become booleans, implications between options).
Golden groups: the defaults, `esModuleInterop=true,forceLong=long,useOptionals=messages`,
`outputServices=grpc-js,env=node`, `snakeToCamel=false,oneof=unions-value,useDate=string,stringEnums=true`,
`outputServices=default,outputServices=nice-grpc,outputClientImpl=grpc-web,useAbortSignal=true,outputIndex=true`,
`nestJs=true,context=true,addGrpcMetadata=true,useDate=string-nano` and
`outputExtensions=true,unknownFields=true,outputTypeRegistry=true,enumsAsLiterals=true,globalThisPolyfill=true,useAsyncIterable=true,outputServices=generic-definitions`.
Beyond the golden groups, the outputs of about 80 other option values and combinations (among them
`useDate=temporal`, `useDuration=string`, `useMongoObjectId`, `rpcBeforeRequest`,
`rpcAfterResponse`, `rpcErrorHandler`, `useContextDataloaders` with batch and `Get` methods,
`outputTypeAnnotations`, `anyType=@type` with `outputTypeRegistry`) were compared with upstream
ts-proto 2.18.1 (run locally with the buf.build image's `package-lock.json`) on the golden inputs
and on scratch inputs with `ObjectId` messages and batch methods. Names that are reserved words
(about 60 JavaScript keywords, strict-mode and TypeScript words, each as a message, an enum, a
service and an imported message, under 8 option sets) give upstream's output or an "unsupported"
error where upstream writes the unformatted text. Golden test `ts-names`: aliased imports used by
helpers (ts-poet keeps the text of a helper from the first file that printed it, like
`Code.toCodeString`) and reserved words that swc accepts where ts-proto puts them.

## Unsupported

Each fails with a `CodeGeneratorResponse.error` that starts with `unsupported by protoc-gen-uni:`
and names the option or input:

- `outputSchema` (the encoded file descriptors and decoded custom options of schema.ts).
- `outputEncodeIncludeTypes` and `outputDecodeIncludeTypes` (JavaScript regular expressions).
- Inputs for which upstream prints invalid TypeScript, which dprint cannot parse, so that ts-poet
  emits the code unformatted (the raw text of ts-proto's templates):
  - a name that is a reserved word (a message named `class`, `function`, `interface`, ...) is supported
    (golden test `naming`) in proto3 files whose fields are singular or oneof members of 32-bit
    integer, bool, string, enum or message types (outside `google.protobuf`), with enums, comments
    and unary services (`outputServices` default with `outputClientImpl` true, false or grpc-web,
    grpc-js, nice-grpc, generic-definitions), with the default method options
    (`outputEncodeMethods`, `outputJsonMethods`, `outputPartialMethods`, `useExactTypes`,
    `initializeFieldsAsUndefined`). Anything else in such a file (proto2 and editions, repeated,
    map, 64-bit, float, bytes and well-known type fields, extensions, streaming methods, `nestJs`,
    `context`, `useNullAsOptional`, `useReadonlyTypes`, `removeEnumPrefix`, ...) is unsupported;
  - malformed oneofs (interleaved oneof fields, a proto3 optional field first or last among other
    fields of a oneof);
  - `nestJs` with `useMapType` and a `google.protobuf.Struct` message;
  - `rpcErrorHandler` with a streaming method and without `rpcAfterResponse`;
  - messages without any `MessageFns` member.

## Known differences

- Upstream reports errors by printing `FAILED!`, the message and the stack to stderr and exiting
  with status 1; the port returns the message in `CodeGeneratorResponse.error`. JavaScript errors
  keep their class prefix (`TypeError: ...`) where the port reproduces them.
- Requests are parsed with protobuf-es semantics (pkg/pb): a repeated singular message field is
  merged and an unknown value of a closed enum is dropped, where ts-proto-descriptors keeps the last
  message and the raw number. protoc and buf never produce such requests.
- Case conversions of names (`snakeToCamel`, `capitalize`, ...) map ASCII letters only; protobuf
  names are ASCII.
