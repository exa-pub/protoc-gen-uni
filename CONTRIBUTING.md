# Contributing

protoc-gen-uni bundles byte-exact Go ports of upstream protoc plugins into one compact binary.
Read [CLAUDE.md](CLAUDE.md) first: the rules there (no extra dependencies, upstream is the reference,
frozen versions) apply to every change.

## Requirements

- Go (version from `go.mod`), `golangci-lint` v2.
- For regenerating test data: network access to buf.build, `xz` and GNU tar. buf itself is pinned
  in `scripts/gen-testdata.sh` and built with `go install` (it does not touch `go.mod`); set
  `BUF=buf` to use your own binary. Set `BUF_TOKEN` — unauthenticated remote plugins are rate limited
  almost immediately.

Running tests needs neither buf, xz nor network: recorded requests are committed in the archives.

## Project structure

```
main.go                                   reads CodeGeneratorRequest from stdin, writes the response
pkg/
  uni/                                    Generator type, uni-name/uni-version parsing, dispatch
  languages/
    go/                                   helpers shared by Go generators (ported protobuf-go internals)
    ecmascript/                           helpers shared by JS/TS generators
      protobuf/                           port of the needed parts of @bufbuild/protobuf
      protoplugin/                        port of @bufbuild/protoplugin
generators/
  registry.go                             explicit list of all generators (sorted)
  generators_test.go                      registry ↔ generators/ ↔ testdata/checksums consistency
  <owner>/<plugin>/README.md              upstream links, versions, options, known differences
  <owner>/<plugin>/<version>/             the port; golden_test.go runs its golden tests
internal/
  goldentest/                             golden test harness
  cmd/protoc-gen-dump/                    records the CodeGeneratorRequest for golden tests
scripts/gen-testdata.sh                   regenerates golden data for one generator version
testdata/
  proto/<test>/                           input buf workspaces shared by all generators
  checksums/<owner>/<plugin>/
    <version>.buf.gen.yaml                committed: inputs = tests (- directory: testdata/proto/<test>),
                                          one remote plugin entry per option group (out: <group>)
    <version>.tar.xz                      committed, built by the script:
      <test>.request.binpb                  the request buf sent to the upstream plugin, parameter cleared
      <group>/parameter.txt                 the parameter buf sent for this option group
      <group>/<test>.sha256                 sha256sum of the upstream output (paths relative to the test dir)
    gen/<version>/<group>/<test>/         NOT committed (.gitignore), optional: upstream output for diffs
```

Go accepts dots in import path elements, so version directories are plain `vX.Y.Z`. The package
name is the sanitized plugin name (`go` → `golang`, `grpc-py` → `grpcpy`); `registry.go` imports it
as `<package>_vX_Y_Z`.

## How golden tests work

1. `scripts/gen-testdata.sh <owner>/<plugin> <version>` runs, for every test in `inputs:` of
   `<version>.buf.gen.yaml`, `buf generate testdata/proto/<test>` with that template (an explicit
   input overrides `inputs:`) — the upstream remote plugins write their output to
   `gen/<version>/<group>/<test>/`, its checksums go to `<group>/<test>.sha256`.
2. It runs the same template once more with every `remote:` replaced by `protoc-gen-dump`
   (`strategy: all`), which stores the request without its parameter as `<test>.request.binpb`
   (checked to be identical for all groups) and the parameter as `<group>/parameter.txt`.
3. It packs requests, parameters and checksums into `<version>.tar.xz` reproducibly (sorted names,
   fixed mtime/owner, `xz -9 -T1`): rerunning with unchanged inputs gives the same bytes.
4. `golden_test.go` → `goldentest.Run` reads the archive, replays each request through `uni.Run`
   with `uni-name`/`uni-version` prepended to the group parameter and compares the sha256 of every
   generated file with `<test>.sha256` (missing, extra and differing files). It also checks that the
   archive matches the tests and groups of `<version>.buf.gen.yaml`.
5. On a mismatch, if `gen/<version>/<group>/<test>/` exists (you ran the script locally), the test
   shows a content diff against it; if that directory does not match the checksums, the test says it
   is stale. Without it you only get "checksum differs" — run the script to get diffs.

Only two small files per generator version are committed: the upstream output is large and
fully determined by the recorded requests, and the requests compress very well together (they
repeat the same imports), so the archive is ~100 KB instead of ~1 MB.

Subtests are named `TestGolden/<group>/<test>`. `UNI_GOLDEN_OUT=<dir>` additionally writes the
actual output to `<dir>/<owner>/<plugin>/<version>/<group>/<test>/`.

`buf.gen.yaml` constraints (checked by the script): every entry is
`- remote: buf.build/<owner>/<plugin>:<version>` with a plain `out: <group>`; no `strategy:`
(buf rejects it for remote plugins). Managed mode applies to the whole template and is recorded in
the request; use it only where upstream output does not embed the changed options.

## Adding a test input

1. Create `testdata/proto/<test>/` as a buf workspace (`buf.yaml`, `version: v2`). Language-specific
   tests are prefixed with the language (`go-features`, `es-validate`). Do not set `go_package`
   (the Go config uses managed mode).
2. Add `- directory: testdata/proto/<test>` to `inputs:` of every generator version that supports
   it and regenerate their test data.

## Adding a new version of an existing generator

1. Copy `generators/<owner>/<plugin>/<old>/` to `<new>/`, rename the import alias, register it in
   `generators/registry.go`, and update the version constant.
2. Copy `testdata/checksums/<owner>/<plugin>/<old>.buf.gen.yaml` to `<new>.buf.gen.yaml`, replace
   the version, run `scripts/gen-testdata.sh <owner>/<plugin> <new>`.
3. Port the upstream diff between the two tags until `go test ./generators/<owner>/<plugin>/<new>/`
   passes. Do not change the old version's behavior; if shared code in `pkg/` must change, all
   golden tests must stay green.
4. Add the version to `generators/<owner>/<plugin>/README.md`.

Go generators format code with `go/printer`, whose output changes between Go releases. The fork
`pkg/languages/go/protogen` uses the printer vendored from the Go version that builds the buf.build
remote plugin (`pkg/languages/go/stdlib/go<version>/`). For a new protoc-gen-go version check the Go
version in `plugins/protocolbuffers/go/<version>/Dockerfile` of
[bufbuild/plugins](https://github.com/bufbuild/plugins); if it changed, vendor that printer too.

## Adding a new generator

1. `generators/<owner>/<plugin>/README.md`: upstream repository, exact source paths at the tag,
   buf.build page, license, options, known differences.
2. `generators/<owner>/<plugin>/<version>/`: `var Generator = uni.Generator{...}` and
   `golden_test.go` (copy from another generator); register in `generators/registry.go`.
3. `testdata/checksums/<owner>/<plugin>/<version>.buf.gen.yaml` with `inputs:` (tests) and option
   groups covering the plugin options, then `scripts/gen-testdata.sh`.
4. Port. Shared language helpers go to `pkg/languages/<language>/`, other shared code to `pkg/`.
   Keep upstream copyright headers on ported code and mention the source file in a comment.

## Dependencies

Only the standard library, `google.golang.org/protobuf` and this module may be imported
(`depguard`). Need something from another library? Port the minimal part into `pkg/`. The single
exception is `github.com/ulikunitz/xz` in `internal/goldentest` (tests only, never linked into the
binary).

## Commits

Squash-merge PRs with a [conventional commit](https://www.conventionalcommits.org/) title — CI
derives the release version from it (`feat:` → minor, `feat!:` → major, otherwise patch).
