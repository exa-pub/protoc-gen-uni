# protoc-gen-uni

A unified code generator (protoc plugin) for protobuf. Work in progress.

## Install

```bash
go install github.com/exa-pub/protoc-gen-uni@latest
```

or download a binary from [Releases](https://github.com/exa-pub/protoc-gen-uni/releases).

## Development

```bash
make test    # go test -race ./...
make lint    # golangci-lint run (config: .golangci.yml)
make build   # ./protoc-gen-uni; VERSION=v1.2.3 to embed a version
make         # lint + test + build
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
