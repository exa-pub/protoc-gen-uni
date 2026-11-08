# apple/swift

C port of `protoc-gen-swift` from [swift-protobuf](https://github.com/apple/swift-protobuf)
(Apache-2.0 with Runtime Library Exception): <https://buf.build/apple/swift>, including buf's
[`extramoduleimports.patch`](https://github.com/bufbuild/plugins/tree/main/plugins/apple/swift)
(the `ExtraModuleImports` option). No Swift toolchain required. Generated code needs the
`SwiftProtobuf` library of the same version.

```yaml
plugins:
  - local: protoc-gen-uni
    out: gen/swift
    opt: [uni-name=apple/swift, uni-version=v1.39.0, Visibility=Public, FileNaming=PathToUnderscores]
```

Options are the same as upstream: [PLUGIN.md](https://github.com/apple/swift-protobuf/blob/1.39.0/Documentation/PLUGIN.md),
plus `ExtraModuleImports=<module>` (repeatable) from the buf patch.

Class: **direct**. Upstream prints the Swift code as text with `CodePrinter` and runs no formatter,
so the port prints the same text.

## Versions

| version | upstream sources |
|---------|------------------|
| v1.39.0 | [`protoc-gen-swift`](https://github.com/apple/swift-protobuf/tree/1.39.0/Sources/protoc-gen-swift), [`SwiftProtobufPluginLibrary`](https://github.com/apple/swift-protobuf/tree/1.39.0/Sources/SwiftProtobufPluginLibrary) (in [`pkg/languages/swift/swift-protobuf`](../../../pkg/languages/swift/swift-protobuf)) |

Each `.c` file names the upstream file it ports.

## Known differences

- `ProtoPathModuleMappings=<file>` is not read: it always fails with
  `failToOpen(path: ...)`, like the remote plugin on buf.build, which cannot see local files.
- Errors where upstream crashes (unknown types or files, out of range indexes, an empty enum,
  messages nested or referencing each other more than 10000 levels deep) go to
  `CodeGeneratorResponse.error`. A request that SwiftProtobuf cannot decode (invalid UTF-8 in a
  string field, a missing required field) is reported on stderr with exit status 1; upstream
  reports it on stderr and exits with status 0 without a response.

## Unsupported

These fail generation with a `CodeGeneratorResponse.error` that starts with
`unsupported by protoc-gen-uni:` and names the input. protoc and buf never produce such names;
the other cases need a deliberately odd `json_name` or option.

- A non-ASCII character or a NUL byte in a name: file names (`name`, `dependency`,
  `file_to_generate`), `package`, the names of messages, fields, oneofs, enums, enum values,
  services and methods, type references (`type_name`, `extendee`, `input_type`, `output_type`),
  `swift_prefix` (after upstream's check that it is a valid Swift identifier) and the
  `default_value` of a non-string, non-bytes field.
  Upstream lowercases all of Unicode and compares strings by Characters and canonical equivalence;
  the port lowercases ASCII and compares bytes, which is the same only for ASCII.
- A `json_name` with U+212A KELVIN SIGN, U+037E GREEK QUESTION MARK or U+1FEF GREEK VARIA: Swift
  finds it equal to the ASCII field name or its default JSON name by canonical equivalence. Other
  non-ASCII `json_name`s are supported.
- A NUL byte in a `json_name`, a reserved name, the `pb.enumvalue.json` option or the
  `ExtraModuleImports` option, and a non-ASCII or NUL `ProtoPathModuleMappings` path (its error
  text quotes the path as Swift does).

Option values are lowercased as Swift does as far as it matters: `Visibility=PAC\u{212A}AGE`
(Kelvin sign) is `package`.
