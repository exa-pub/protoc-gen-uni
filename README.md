# protoc-gen-uni

One compact protoc plugin that bundles byte-exact Go ports of popular protobuf plugins — no Node.js,
Python or other runtimes needed. Pick the generator with `uni-name` (as on buf.build) and
`uni-version`; all other options go to the generator unchanged:

```yaml
# buf.gen.yaml
version: v2
plugins:
  - local: protoc-gen-uni
    out: gen/ts
    opt: [uni-name=bufbuild/es, uni-version=v2.17.0, target=ts]
  - local: protoc-gen-uni
    out: gen/go
    opt: [uni-name=protocolbuffers/go, uni-version=v1.36.12, paths=source_relative]
```

## Generators

| uni-name | versions | upstream |
|----------|----------|----------|
| [`bufbuild/es`](generators/bufbuild/es/README.md) | v2.17.0 | [protoc-gen-es](https://buf.build/bufbuild/es) |
| [`protocolbuffers/go`](generators/protocolbuffers/go/README.md) | v1.36.12 | [protoc-gen-go](https://buf.build/protocolbuffers/go) |

## Install

```bash
go install github.com/exa-pub/protoc-gen-uni@latest
```

or download a binary from [Releases](https://github.com/exa-pub/protoc-gen-uni/releases).

## Development

See [CONTRIBUTING.md](CONTRIBUTING.md) for the project structure and how to add generators.

```bash
make test    # go test -race ./...
make lint    # golangci-lint run (config: .golangci.yml)
make build   # ./protoc-gen-uni; VERSION=v1.2.3 to embed a version
make         # lint + test + build
scripts/gen-testdata.sh bufbuild/es v2.17.0  # regenerate test data from upstream (see CONTRIBUTING.md)
```

## CI and releases

[`.github/workflows/ci.yml`](.github/workflows/ci.yml): `version` → `lint` + `test` → `release`.

- **version** — the target version is fixed at the start of the run: latest `vX.Y.Z` tag + bump based on
  [conventional commits](https://www.conventionalcommits.org/) (`feat!:` / `BREAKING CHANGE:` → major,
  minor before 1.0; `feat:` → minor; anything else → patch). No tags yet → `v0.1.0`. Pull requests get
  `vX.Y.Z-g<sha>`.
- **lint** — golangci-lint; **test** — `go mod tidy -diff`, build with the version embedded, `go test -race`.
- **release** — push to `main` only: binaries for linux/darwin/windows, `vX.Y.Z` tag and a GitHub Release
  with generated notes.

The version is derived from commit subjects on `main`, so squash-merge pull requests with a
conventional-commit title.
