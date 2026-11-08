# Swift standard library

The behavior of the Swift 6 standard library that shows in the output of the Swift-hosted
plugins, where it works on Characters (extended grapheme clusters) instead of bytes:
`String.split(separator:maxSplits:)`, `dropFirst(_:)` and splitting at `Character.isNewline`
(`swift_std.h`, prefix `swift_`).

Grapheme cluster breaking needs the Unicode tables of the Swift runtime, which are not ported.
The functions are exact for ASCII and for a conservative set of characters that join their
neighbors only by combining marks (Latin, Greek, Cyrillic, CJK, kana, Hangul syllables, common
symbols and emoji, and the combining marks U+0300–U+036F, U+20D0–U+20F0, U+FE00–U+FE0F,
U+FE20–U+FE2F). On other characters they fail with `unsupported by protoc-gen-uni: ...` rather
than give a different result.

Used by [`../grpc-codegen`](../grpc-codegen) and
[grpc/swift-protobuf](../../../../generators/grpc/swift-protobuf/README.md). Tests:
`tests/swift_std_test.c`.
