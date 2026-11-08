# Rules

They apply to every change.

- **Upstream is the reference.** Checksums and upstream output in `testdata/` come only from the
  upstream plugin (`scripts/gen-testdata.sh`). Never edit them or produce them from our output. If a
  golden test fails, fix the port.
- **Byte for byte**: headers with the upstream version, option parsing, error texts,
  `supported_features`, editions.
- **Unsupported means an error.** If a port does not implement something upstream accepts (an
  option, a feature, an input), generation fails with a `CodeGeneratorResponse.error` that names it.
  It never produces a silently different output. The generator README lists it.
- **Versions are frozen**: `generators/<owner>/<plugin>/<version>/` is exactly that upstream
  version. `pkg/` has no version directories: a component implements the newest upstream version in
  use, and only a part that differs for an older one is versioned (`strconv_go1.21.6.c`, or a
  `<version>/` directory). Changes in `pkg/` must keep the golden tests of *all* versions green.
- **No third-party code**: only the libc headers allowed by `scripts/check-includes.sh`.
- Ported code keeps upstream license headers and names the upstream file it ports. Write code and
  docs in English.
- Input is untrusted: reads are bounds-checked and recursion is limited. Bad input must give an
  error, never a crash or hang. Builds stay clean with `-Werror`, ASan/UBSan and the fuzzer.

## C conventions

- Arena: a process serves one request and exits, so memory comes from `xalloc`
  (`pkg/core/util.h`) and is never freed. Code ported from garbage-collected languages keeps the
  shape of the original (no ownership or cleanup code), `fail()` unwinds without leaks, allocation
  is a pointer bump and `free` never runs. The price is peak memory: every allocation lives until
  exit, so allocate in proportion to the input.
- Errors: `fail()` longjmps to the nearest `try_run()`, which works like panic/recover.
- State lives no longer than the request: the fuzz harness serves many requests per process and
  frees the arena between them. File-scope variables are set at the generator's entry point.
- Mark code that runs once per request or declaration with `COLD`; size matters.
- Never look data up by the address of a constant: `-fmerge-all-constants` may merge equal ones.
