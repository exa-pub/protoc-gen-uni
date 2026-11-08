# black 24.3.0, C port

The Python formatter that community/danielgtaylor-betterproto runs over its rendered template:
`black.format_str(src, mode=black.FileMode(target_versions={TargetVersion.PY37}))` with black
24.3.0 on Python 3.11 (default mode: line length 88, string normalization, magic trailing commas,
no preview features). Upstream: <https://github.com/psf/black/tree/24.3.0> (MIT, Copyright (c)
2018 Łukasz Langa). The ported parts of its `src/blib2to3/` (`pytree.py`, the token constants of
`pgen2/token.py`) are under the Python Software Foundation License (`src/blib2to3/LICENSE`);
`pytree.py` carries "Copyright 2006 Google, Inc. All Rights Reserved. Licensed to PSF under a
Contributor Agreement."

## Layout

Formatter library (docs/LAYOUT.md): no tokenizer and no parser (blib2to3 is not ported). A
generator builds the blib2to3 tree that black would parse from the source, with the builders of
`black.h`, and `bk_format_str` formats it.

- `tree.c`: the parts of `blib2to3/pytree.py` black uses, and node construction (a symbol node
  with one child collapses to it, like `pytree.convert`).
- `nodes.c`: `black/nodes.py` (`whitespace()`, node classification).
- `brackets.c`: `black/brackets.py` (`BracketTracker`, delimiter priorities).
- `strings.c`, `comments.c`: docstring indentation (`fix_docstring`), comments in prefixes
  (`list_comments`, `generate_comments`, `make_comment`).
- `lines.c`: `black/lines.py` (`Line`, `EmptyLineTracker`, the checks of the splits).
- `linegen.c`: `black/linegen.py` (`LineGenerator`, `normalize_invisible_parens`,
  `transform_line`, `rhs`, `left_hand_split`, `delimiter_split`, `run_transformer` with its
  second opinion). Python generators are evaluated eagerly where their consumers touch disjoint
  leaves; `generate_trailers_to_omit` stays a resumable iterator because the splits it drives
  change leaves it has not looked at yet. Exceptions (`CannotSplit`) are `false` results.
- `black.c`: `format_str` and `_format_str_once`, unity build. format_str formats its own output a
  second time; the second pass runs on the same tree after `prepare_second_pass` has made it the
  tree blib2to3 parses from the first output: trailing commas added by the splits become leaves
  (in the list node of their brackets), and the first tokens of the lines get the blank lines and
  comments of the output as prefix.
- `build.c`: builders of the grammar productions generated code uses.

## Building trees as blib2to3 parses them

- Prefixes: blib2to3 puts the text between two tokens (spaces, blank lines, comment lines) in the
  prefix of the second. Comments and blank lines before the first statement of a block belong to
  that statement's first token, those after a block to the next statement (`driver.py`
  `_partially_consume_prefix`), so a generator sets the prefix of each statement's first token
  (`bk_stmt`, `bk_classdef`, ... take it) and the trailing text as the prefix of ENDMARKER.
- Black keeps only the comments and the number of newlines of a prefix; the newlines decide the
  blank lines it keeps (`EmptyLineTracker`).
- Node kinds follow Grammar.txt: `f(a, b,)` has an `arglist` with the trailing comma, `x: T = v`
  is `expr_stmt(NAME, annassign)`, `async def` is `async_stmt(ASYNC, funcdef)`, `-1` is
  `factor(MINUS, NUMBER)`.

Not ported (generated code has none; a tree with them fails or is outside the contract of
`black.h`): trailing comments on code lines, `# fmt: off/skip`, `with` and `match` statements,
power operators (`hug_power_op`), preview features, string prefixes and quote normalization of
strings that need it (STRING leaves must be double-quoted without backslashes or inner quotes,
or triple-quoted docstrings), numeric literal normalization (decimal integers only), East Asian
widths (`str_width` counts code points: non-ASCII text may only appear in comments and in strings
that are a statement of their own, whose layout does not depend on width).

## Tests

`tests/black_test.c` (`unit/black_test`): node fixtures whose expected text is the output of
black 24.3.0 (Python 3.11.4) for the same source, covering the splits of the betterproto template
(right hand splits with and without optional parentheses, delimiter splits with added trailing
commas, imports, class and def lines, magic trailing commas), docstrings, comments and empty
lines. The golden tests of community/danielgtaylor-betterproto (`python-long` has long names,
types and comments) exercise the rest.
