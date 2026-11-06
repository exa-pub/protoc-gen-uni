# protoc-gen-uni

One protoc plugin written in Go that bundles **byte-exact Go ports** of popular upstream plugins
(named as on buf.build: `<owner>/<plugin>`), each at an exact version. The generator is selected by
two parameters, everything else is passed to it unchanged:

```yaml
plugins:
  - local: protoc-gen-uni
    out: gen
    opt: [uni-name=bufbuild/es, uni-version=v2.17.0, target=ts]
```

## Key idea: compact, no extra dependencies

The whole point is a **single small static binary without extra dependencies**: no Node.js, Python,
protoc or buf at runtime; the binary links only the standard library and `google.golang.org/protobuf`.
Whatever an upstream plugin needs (its plugin framework, runtime helpers, internal packages) is
ported by hand into `pkg/` or into the generator itself. Do not add modules to `go.mod`. The only
exception is `github.com/ulikunitz/xz`, used by the test harness `internal/goldentest` to read
archives; `depguard` in `.golangci.yml` enforces this.

## Rules

- **Upstream is the reference.** Archives in `testdata/checksums` (and the optional, uncommitted
  upstream output in `testdata/checksums/*/*/gen`) are produced only by the upstream plugin
  (`scripts/gen-testdata.sh`, buf.build remote plugins). Never edit them or write them from our
  output; when a golden test fails, fix the port.
- **Output must match byte for byte**, including headers with the upstream version, option parsing,
  error texts, `supported_features` and supported editions.
- **Versions are frozen.** `generators/<owner>/<plugin>/<version>/` reproduces exactly that upstream
  version. A new upstream version = copy the previous version directory and port the upstream diff.
  Changes to shared code in `pkg/` must keep golden tests of *all* versions green.
- Ported code keeps upstream copyright/license headers.

## Layout

- `main.go` — reads the request, `pkg/uni` dispatches it.
- `pkg/uni` — `Generator` type, `uni-name`/`uni-version` parsing, dispatch.
- `generators/registry.go` — explicit list of all generators.
- `generators/<owner>/<plugin>/<version>/` — one port; `golden_test.go` runs its golden tests.
- `generators/<owner>/<plugin>/README.md` — upstream links, supported versions and options.
- `pkg/languages/<language>/…` — helpers shared by generators of one language
  (`go`, `ecmascript`); `pkg/…` — other shared libraries.
- `internal/goldentest` — golden test harness; `internal/cmd/protoc-gen-dump` — records requests.
- `testdata/proto/<test>/` — shared input buf workspaces.
- `testdata/checksums/<owner>/<plugin>/<version>.buf.gen.yaml` (committed) — `inputs:` = tests
  (`testdata/proto/<test>`), one `remote:` plugin entry per option group (`out: <group>`).
- `testdata/checksums/<owner>/<plugin>/<version>.tar.xz` (committed, built by the script) —
  `<test>.request.binpb` recorded requests (shared by groups), `<group>/parameter.txt`,
  `<group>/<test>.sha256` checksums of the upstream output.
- `testdata/checksums/<owner>/<plugin>/gen/<version>/<group>/<test>/` — upstream output,
  **not committed**; created by the script, used by tests to show diffs when present.

See CONTRIBUTING.md for workflows.

## Commands

```bash
make test                                        # go test -race ./...
make lint                                        # golangci-lint run
scripts/gen-testdata.sh bufbuild/es v2.17.0      # regenerate <version>.tar.xz + gen/ (needs network, xz, BUF_TOKEN)
go test ./generators/bufbuild/es/... -run 'TestGolden/ts/proto3'
UNI_GOLDEN_OUT=/tmp/out go test ./generators/... # also dump actual output for `diff -r`
```
