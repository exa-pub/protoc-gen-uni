# grpc/web

C port of `protoc-gen-grpc-web` from [gRPC-Web](https://github.com/grpc/grpc-web) (Apache-2.0):
<https://buf.build/grpc/web>. No protoc required. Generated code needs the `grpc-web` npm package
2.1.1 and the message classes of `google-protobuf` (protoc-gen-js, buf.build/protocolbuffers/js),
which this plugin does not generate.

```yaml
plugins:
  - local: protoc-gen-uni
    out: gen/js
    opt: [uni-name=protocolbuffers/js, uni-version=v4.0.3, import_style=commonjs]
  - local: protoc-gen-uni
    out: gen/js
    opt: [uni-name=grpc/web, uni-version=v2.1.1, import_style=commonjs+dts, mode=grpcweb]
```

Per file with services: `<file>_grpc_web_pb.js` (Closure or CommonJS clients), or
`<Dir>/<File>ServiceClientPb.ts` with `import_style=typescript`. With `commonjs+dts` and
`typescript`, every file also gets `<file>_pb.d.ts` (TypeScript declarations of its messages and
enums), and `commonjs+dts` adds `<file>_grpc_web_pb.d.ts`. See the
[gRPC-Web basics](https://grpc.io/docs/platforms/web/basics/).

**Class: direct** ([LAYOUT.md](../../../docs/LAYOUT.md)). The plugin prints JavaScript and
TypeScript with the legacy `io::Printer::Print` API of libprotoc and runs no formatter: the port
prints the same text.

## Options

As upstream (`GeneratorOptions::ParseFromOptions`), parsed per file with `ParseGeneratorParameter`;
an error is prefixed with the file name (`CodeGenerator::GenerateAll`).

| option | effect |
|--------|--------|
| `mode=grpcwebtext` | `application/grpc-web-text` clients (`format = 'text'`) |
| `mode=grpcweb` | `application/grpc-web+proto` clients (`format = 'binary'`) |
| `mode=jspb`, `mode=binary` | Google-internal OP clients (`/$rpc/` paths); `jspb` uses `serialize`/`deserialize` |
| `import_style=closure` | default: `goog.provide`/`goog.require` |
| `import_style=commonjs` | `require()` and `module.exports` |
| `import_style=commonjs+dts` | CommonJS plus the `.d.ts` files |
| `import_style=typescript` | `<File>ServiceClientPb.ts` plus `<file>_pb.d.ts` |
| `import_style=experimental_closure_es6` | Closure plus `<file>.pb.grpc-web.js` (aborts upstream, see below) |
| `out=<name>` | the output file name (not with `typescript`) |
| `multiple_files=True` | with Closure: one file per method descriptor and two client files (aborts upstream, see below) |
| `goog_promise=True` | with `mode=jspb`: `grpc.web.promise.GrpcWebPromise` instead of `Promise` |
| `plugins=<name>` | with `multiple_files=True`: `goog.require('<name>....MethodDescriptor')` |

`mode` is required (`options: mode is required`); another mode gives
`options: invalid mode - <mode>`, another import style `options: invalid import_style - <style>`
and another option `unsupported option: <option>`. The header records `protoc v<x.y.z>` from
`CodeGeneratorRequest.compiler_version` (`v0.0.0` from buf, which sends none).

## Versions

| version | upstream sources | libprotoc |
|---------|------------------|-----------|
| v2.1.1 | [`javascript/net/grpc/web/generator/grpc_generator.cc`](https://github.com/grpc/grpc-web/blob/2.1.1/javascript/net/grpc/web/generator/grpc_generator.cc) | protobuf 29.1 ([`MODULE.bazel`](https://github.com/grpc/grpc-web/blob/2.1.1/MODULE.bazel)): [`pkg/languages/cpp/libprotoc`](../../../pkg/languages/cpp/libprotoc/libprotoc.h) with its 29.1 version (`libprotoc_29.1.c`, on top of 33.4) |

`v2.1.1/grpc_generator.c` ports `grpc_generator.cc` and `v2.1.1/generator.c` its `main`. The
remote plugin builds the release source tarball with Bazel
([bufbuild/plugins](https://github.com/bufbuild/plugins/tree/main/plugins/grpc/web/v2.1.1)) in the
default `fastbuild` mode, so the debug checks of libprotoc are on. protobuf 29.1 compiles the
feature defaults for the generator's own editions (proto2 to 2024) and does not know the 2024
features `enforce_naming_style` and `default_symbol_visibility`; editions up to 2024 are accepted.

## Known differences

- Invalid descriptors: upstream logs the errors of the descriptor pool to stderr (with
  timestamped log prefixes) and exits with status 1. The port puts the same text (without the log
  prefixes) in `CodeGeneratorResponse.error`. The template leaves out the `naming` input for this
  reason.
- `multiple_files=True` with Closure (when a service has a method) and
  `import_style=experimental_closure_es6` (when the file has a service) print the `$version$`
  variable without defining it: the debug check of `io::Printer` aborts upstream with a stack trace
  and no response. The port returns `/protoc-gen-grpc-web: undefined variable: "version"` in
  `CodeGeneratorResponse.error`.

## Unsupported

Nothing: every option and input upstream accepts is supported.
