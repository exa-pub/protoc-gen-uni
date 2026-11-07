# bufbuild/es

C port of `protoc-gen-es` from [Protobuf-ES](https://github.com/bufbuild/protobuf-es)
(Apache-2.0): <https://buf.build/bufbuild/es>. No Node.js required. Generated code needs the
`@bufbuild/protobuf` runtime of the same version.

```yaml
plugins:
  - local: protoc-gen-uni
    out: gen/es
    opt: [uni-name=bufbuild/es, uni-version=v2.17.0, target=ts]
```

Options are the same as upstream: [plugin options](https://github.com/bufbuild/protobuf-es/blob/v2.17.0/MANUAL.md).

## Versions

| version | upstream sources |
|---------|------------------|
| v2.17.0 | [`protoc-gen-es`](https://github.com/bufbuild/protobuf-es/tree/v2.17.0/packages/protoc-gen-es/src), [`protoplugin`](https://github.com/bufbuild/protobuf-es/tree/v2.17.0/packages/protoplugin/src), parts of [`protobuf`](https://github.com/bufbuild/protobuf-es/tree/v2.17.0/packages/protobuf/src) |

Each `.c` file names the upstream file it ports.

## Known differences

- Errors go to `CodeGeneratorResponse.error`. Upstream prints them to stderr and exits with
  status 1.
- A NUL byte ends file and package names. protoc and buf never produce one.
- The TypeScript transpilation fallback of `@bufbuild/protoplugin` is not ported, because
  protoc-gen-es never uses it.
