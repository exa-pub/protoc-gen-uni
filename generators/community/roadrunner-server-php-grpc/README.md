# community/roadrunner-server-php-grpc

C port of `protoc-gen-php-grpc` from [roadrunner-server/grpc](https://github.com/roadrunner-server/grpc)
(MIT): <https://buf.build/community/roadrunner-server-php-grpc>. No Go toolchain required. It
generates a PHP interface (`<Service>Interface extends GRPC\ServiceInterface`) per service for the
[RoadRunner](https://roadrunner.dev) application server; the messages come from
[protocolbuffers/php](../../protocolbuffers/php/README.md).

```yaml
plugins:
  - local: protoc-gen-uni
    out: gen/php
    opt: [uni-name=community/roadrunner-server-php-grpc, uni-version=v6.0.0]
```

## Versions

| version | upstream sources | Go |
|---------|------------------|----|
| v6.0.0 | [`protoc_plugins/protoc-gen-php-grpc`](https://github.com/roadrunner-server/grpc/tree/v6.0.0/protoc_plugins/protoc-gen-php-grpc) (`main.go`, `php/`), protobuf-go v1.36.12 | 1.27.1 |

Each `.c` file names the upstream file it ports. The request is decoded with the protobuf-go
semantics of [`pkg/languages/go/protodesc`](../../../pkg/languages/go/protodesc) (required fields,
10000 levels of nesting, no extension types: the plugin links only `descriptorpb` and `pluginpb`).
The case mappings of `Camelize` (`unicode.IsUpper`, `ToUpper`, `ToLower`: Unicode 17.0.0) and
`strings.ToLower` are those of Go 1.27.1, ported in [`pkg/languages/go/std`](../../../pkg/languages/go/std).

**Class: direct** ([LAYOUT.md](../../../docs/LAYOUT.md)). The plugin executes one `text/template`
and runs no formatter: the template becomes C code with the same whitespace (`template.c`).

## Options

None: the parameter is ignored, as upstream. The plugin writes `<namespace>/<Service>Interface.php`
for every service of every file in the request, imports included, and declares
`FEATURE_PROTO3_OPTIONAL` (no editions).

## Unsupported

- Generated files larger than 256 MiB in total fail with the `CodeGeneratorResponse.error`
  `unsupported by protoc-gen-uni: generated files larger than 256 MiB in total`. Every input and
  output type repeats the `php_namespace` of its file, so the output grows with the product of
  the namespace length and the number of methods; upstream would print it all.
- Messages nested so deeply that decoding them needs more than 256 KiB of stack (around a thousand
  levels; upstream allows 10000, protoc and buf stop far below) fail with the
  `CodeGeneratorResponse.error` `unsupported by protoc-gen-uni: messages nested too deeply (over
  256 KiB of decoder stack)`.

## Known differences

- Upstream panics (a goroutine trace on stderr, exit status 2) on a request it cannot decode and on
  a service without a name or a method without an input or output type. The port prints the first
  line of the panic to stderr after the prefix `protoc-gen-uni: parse request: ` and exits with
  status 1, e.g. `protoc-gen-uni: parse request: panic: template: phpBody:15:15: executing
  "phpBody" at <name $ns $m.InputType>: error calling name: runtime error: invalid memory address
  or nil pointer dereference`. The decoding errors of protobuf-go start with `proto: ` followed by
  a space or a no-break space, chosen by a hash of the plugin binary (`internal/detrand`); the port
  prints a space.
