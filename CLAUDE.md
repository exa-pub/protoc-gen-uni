# protoc-gen-uni

One protoc plugin in C11 that bundles **byte-exact ports** of upstream plugins, each at an exact
version, named as on buf.build. `uni-name` and `uni-version` select the generator; all other
options go to it unchanged:

```yaml
plugins:
  - local: protoc-gen-uni
    out: gen
    opt: [uni-name=bufbuild/es, uni-version=v2.17.0, target=ts]
```

## Goals, rules and layout

@docs/GOALS.md

@docs/RULES.md

@docs/LAYOUT.md

## Commands

```bash
scripts/docker.sh cmake --workflow --preset linux-x86_64   # build + all tests (toolchain image)
scripts/docker.sh cmake --workflow --preset asan           # ASan + UBSan
ctest --test-dir build/linux-x86_64 -R golden/bufbuild/es/v2.17.0/proto3 --output-on-failure
scripts/gen-testdata.sh bufbuild/es v2.17.0                # upstream checksums (network, BUF_TOKEN)
```

Workflows (new version or generator, test data, fuzzing, size) are in CONTRIBUTING.md.
