# b6cpp — C preprocessor for the BESM-6

The C preprocessor for the **BESM-6 Unix v7 port**, installed as `b6cpp`. It is a *host*
tool: it runs on the build machine and preprocesses C source (`#include`, macro definition
and expansion, conditional compilation) before that source reaches the BESM-6 assembler and
compiler.

The engine descends from John F. Reiser's fast Unix v7 `cpp`, but it has been substantially
modernized: it now targets **C11 (N1570)** and is validated by a conformance test suite
under [`test/`](test) that currently passes in full. It is no longer the pre-ANSI cpp its
ancestor was — it understands variadic macros, `#`/`##`, `_Pragma`, the C11 predefined
macros, and more (see below).

## Building

`b6cpp` is part of the top-level `cmd/` toolchain build. From the repository root:

```sh
make            # configure + build every cmd/ tool, including b6cpp
make install    # install it as bin/b6cpp
make run        # build and run the unit tests via ctest
```

Do **not** invoke `cc`/`clang` or `cmake --build` by hand — always go through the top-level
`make` targets. The tool is compiled with the host C/C++ toolchain (C++17) under
`-Wall -Werror -Wshadow`. The local [`CMakeLists.txt`](CMakeLists.txt) links the engine from
`cpp.c buffer.c scan.c direct.c macro.c diag.c parser.c yylex.c`.

## Usage

```text
cpp [options] [infile [outfile]]
```

With no file arguments it reads standard input and writes standard output; the first
positional argument is the input file, the second is the output file. **The process exit
status is the number of errors reported** (0 on success).

| Option | Meaning |
| --- | --- |
| `-I path` | Add a directory to the header search list (up to 8; `/usr/include` is searched last). |
| `-D name[=value]` | Predefine a macro before processing; bare `-Dname` defines it as `1`. Up to 20. |
| `-U name` | Undefine a macro at startup. Up to 20. |
| `-R` | Allow macro recursion (disables the "blue paint" recursion stop). |
| `-P` | Suppress the `# line "file"` line markers in the output. |
| `-C` | Keep comments in the output instead of discarding them. |
| `-w` | Suppress warnings. |
| `-trigraphs` | Enable translation-phase-1 trigraph replacement (off by default). |
| `-E` | Accepted and ignored, for compatibility. |

(`-w` and `-trigraphs` are supported even though the built-in `usage()` help text does not
list them.)

## Directives

All of the standard directives are supported:

`#include` (both `<header>` and `"header"` forms), `#define`, `#undef`, `#if`, `#ifdef`,
`#ifndef`, `#elif`, `#else`, `#endif`, `#line`, `#error`, and `#pragma`. Unknown pragmas are
accepted and ignored. Leading whitespace before the `#` is allowed (C11 §6.10).

## Macros

- **Object-like** and **function-like** macros.
- **Variadic** macros with `__VA_ARGS__`, GNU-style **named varargs** (`#define M(args...)`),
  and GNU **comma elision** (`, ## __VA_ARGS__` drops the comma when the variadic part is
  empty).
- The **`#` stringize** and **`##` token-paste** operators, with their C11 constraints
  (`#` must precede a parameter; `##` may not begin or end a replacement list).
- **Rescanning with recursion prevention** — the "blue paint" rule of §6.10.3.4, so a macro
  is never re-expanded within its own expansion. (`-R` overrides this.)
- A wrong argument count to a function-like macro is an **error**. An identical redefinition
  is accepted silently; an incompatible redefinition is **warned**.

### Predefined macros

`__LINE__` and `__FILE__` are synthesized per expansion, and the `_Pragma` operator
(§6.10.9) is supported. The fixed C11 set is also provided:

| Macro | Value |
| --- | --- |
| `__STDC__` | `1` |
| `__STDC_VERSION__` | `201112L` |
| `__STDC_HOSTED__` | `1` |
| `__DATE__` | `"Mmm dd yyyy"` |
| `__TIME__` | `"hh:mm:ss"` |
| `__STDC_NO_COMPLEX__` | `1` |
| `__STDC_NO_ATOMICS__` | `1` |
| `__STDC_NO_THREADS__` | `1` |
| `__STDC_NO_VLA__` | `1` |

The four `__STDC_NO_*` macros are §6.10.8.3 conditional-feature macros, and they are what makes
`__STDC_HOSTED__ 1` honest: §4p6 would otherwise oblige the implementation to ship `<complex.h>`,
`<stdatomic.h>` and `<threads.h>`, and it ships none of them — the BESM-6 has one native float
format and no complex type, no atomic instructions, and no threads. `__STDC_NO_VLA__` reports the
front end, which folds every array dimension to a literal and rejects one it cannot.

All predefined macros are protected: `#define`/`#undef` of them (and `-D`/`-U` on them) is
rejected per §6.10.8.4. Optional OS/architecture macros (`unix`, `vax`, …) exist as
compile-time conditionals but are off in this build.

## `#if` expressions

`#if`/`#elif` conditions are evaluated by a full integer constant-expression evaluator
(recursive descent with precedence climbing, in `parser.c`/`yylex.c`). It supports the
arithmetic, bitwise, shift, relational, equality, and logical operators, the ternary `?:`,
and the comma operator, plus both `defined name` and `defined(name)`. Operands may be
decimal, octal, or hexadecimal integer literals (with an `L` suffix) or character constants;
an undefined identifier evaluates to `0`. Assignment (`=`) is rejected, and division or
modulo by zero is diagnosed.

## C11 conformance and limits

`b6cpp` targets N1570 and is exercised by the conformance suite in [`test/`](test). It
honors the C11 §5.2.4.1 translation-limit minimums; the relevant sizes (from
[`defs.h`](defs.h)) are:

| Limit | Value |
| --- | --- |
| Logical source line length | ≥ 4095 characters |
| Simultaneously defined macros | 4095 minimum (hash table `symsiz` = 6151) |
| Macro parameters | up to 127 |
| `#include` nesting depth | 10 |
| `#if` nesting depth | 64 |

Trigraph translation (translation phase 1) is available via `-trigraphs`.

Identifiers are significant to their full length (no truncation). Bytes 0x80–0xFF
are accepted as identifier characters, so raw UTF-8 names such as `#define длина 100`
work — including function-like macro and parameter names. This matches GCC/Clang's
default handling of the implementation-defined extended identifier set (§6.4.2).

## Source layout

| File | Responsibility |
| --- | --- |
| `cpp.c` | Entry point: state init, character-table setup, option parsing, registration of the built-in directives and predefined macros. |
| `buffer.c` | I/O and the sliding scan buffer: refill from files/pushback, output flushing, macro pushback storage, trigraph translation. |
| `scan.c` | The lexical scanner — tokenizes input and drives comment/string/line-continuation handling. |
| `macro.c` | Macro definition, the symbol table, and macro expansion (argument collection, `#`/`##`, blue paint). |
| `direct.c` | Directive dispatch and `#include` file search / include stack. |
| `parser.c`, `yylex.c` | The `#if` constant-expression evaluator and its tokenizer. |
| `diag.c` | Diagnostics (error/warning counting) and small string helpers. |
| `defs.h` | The central state struct, the symbol-table entry type, and the sizing limits. |
| `intern.h` | Scan-table test macros, blue-paint marker bytes, and the superimposed-code macro-name filter. |

## Testing

`make run` builds and runs the GoogleTest C11 conformance suite under [`test/`](test) via
ctest. The suites derive from a single `PreprocessorTest` fixture
([`test/test_support.h`](test/test_support.h)) that spawns the built `b6cpp` in a temporary
directory, normalizes its output, and exposes the `EXPECT_TOKENS`, `EXPECT_PP_OK`, and
`EXPECT_PP_DIAGNOSES` matchers.
