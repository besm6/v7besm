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

### A directive after `#line`

**`#line` used to swallow the directive on the line after it** — silently, and whatever it was.
`#line 11` followed by `#define AA 7` left the `#define` in the output as text and `AA`
undefined.

`process_directives` hands control back at each `#` and resumes with a *fast* scan looking for
`\n#`. Every arm but one reaches the shared drain at the foot of the loop, which leaves the scan
pointer **on** the newline that ends the directive line, so the next line's `#` is found. The
`#line` arm drained its own operands — it has to collect them for the macro expansion §6.10.4p5
requires — and left the pointer one character further on, *past* that newline. One line backs it
up again.

Nothing had met it: `#line` is written by generators rather than by hand, and the generators that
write it put a comment or a blank line next. `b6yacc` does not. It emits `# line N "file"`
immediately above the `%{ … %}` block it copies out of the grammar, and `cmd/lex/parser.y` opens
that block with `#include "ldefs.h"` — so the native `lex` of task C10d was the first thing in
this tree to compile a file with a directive in that position, and it failed at `b6parse` with
`Empty type specifier list … lexeme: include`. `Line.*` in [`test/`](test/) covers it now.

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

**`besm6` is defined unconditionally**, and it is the one predefine that does not describe the
machine the preprocessor is *running* on: this tool always targets the BESM-6, whatever it was
compiled for. It is an ordinary macro, freely `#undef`-able like `unix`, and it is what lets one
source tune itself to the target — the next section is the first user of it.

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

**Everything in this section is the HOST build.** The same sources are also built for the
BESM-6, and that build meets none of those minima — see the next section.

Identifiers are significant to their full length (no truncation). Bytes 0x80–0xFF
are accepted as identifier characters, so raw UTF-8 names such as `#define длина 100`
work — including function-like macro and parameter names. This matches GCC/Clang's
default handling of the implementation-defined extended identifier set (§6.4.2).

## Building for the BESM-6

These same eight sources are built a **second** time, by the `b6*` cross toolchain, into
`build/rootfs/usr/bin/cpp` — the preprocessor that runs on the machine, and the first step of
self-hosting (task **C9a** in [../TODO.md](../TODO.md); the plan is [TODO.md](TODO.md)).
[`rootfs/CMakeLists.txt`](rootfs) is the whole of the build machinery, and there is **no second
copy of any source**: the only thing that differs is a size profile in [`defs.h`](defs.h) keyed
on the `besm6` predefine above.

**That profile is deliberately non-conforming.** It does not meet the C11 §5.2.4.1 minima and
cannot: a user program on this machine gets 28,672 words of `const+text+data+bss`, no pointer
reaches past word 32,767, the stack is **4,096 words and nothing checks it**, and no struct may
exceed 4,096 words. The C11 conformance results above are the **host** build's alone.

| | host | BESM-6 | why |
| --- | ---: | ---: | --- |
| `BUFSIZ` (logical line / scan window) | 8192 | 1024 | address space |
| `SBSIZE` (side buffer: every macro's name + body) | 65536 | 24576 | 4,096 words, the largest object in the program; measured — one kernel source's include closure is 315 macros and ~14 KB of them, and 8192 overflowed on `<sys/reg.h>` |
| `SYMSIZ` (hash slots) | 6151 | 1021 | 315 macros is the measured load; 4095 would cost 12,285 words |
| `MAXFRM` (macro parameters) | 127 | 31 | 254 words of **stack** per nesting level, against 4,096 |
| `MAXARGDEPTH` (macro call inside a macro argument) | 200 | **1** | the stack; see below |
| `EXPTXT_MULT` | 4 | 2 | keeps the heap block inside one 1,024-word `malloc` page |

### Where the ceilings actually bind

Four things had to change in the shared source before any of it fit, and each is worth knowing
because **every v7 program of this shape meets the same wall**:

1. **No struct may exceed 4,096 words** — a member is named by a 12-bit offset from a base
   register and there is no longer form, so `b6as` refuses the offset and nothing downstream can
   rescue it. `struct cppstate` was ~38,630 words. The fix is not to shrink it but to take the
   four big arrays *out* of it (`arena`, `side_buf`, `symbols`, `paint_stack` are at file scope
   in [`cpp.c`](cpp.c) now); a file-scope array of any size is reached through an index register
   and has no such limit. This is [../README.md](../README.md)'s hazard list entry.
2. **The scratch buffers are on the heap, not in a frame.** `expand_macro`'s `acttxt`/`exptxt`/
   `strbuf` and `expand_text`'s `subarena` were 7×`BUFSIZ` and 2×`BUFSIZ` of *automatic* storage
   on a path that recurses.
3. **The startup phases are separate functions.** `main()` stays on the stack under every macro
   expansion; as one function its frame was **531 words**, split into `build_scan_tables()`,
   `parse_args()` and `register_builtins()` it is **41**.
4. **`#line`'s and `#error`'s buffers are `static`.** `process_directives()` is the top-level
   loop, entered once and never recursively, and its four `BUFSIZ` automatics sat under
   everything.

### The measured frame chain

Read out of `build/cmd/cpp/rootfs/cpp.dis` — the `15 utm 0NNN` in each prologue, which is what
[../grep/README.md](../grep/README.md) means by reading the frame rather than estimating it:

| | words |
| --- | ---: |
| `main` | 41 |
| `process_directives` | 372 |
| `scan_token` | 656 |
| `lookup_token` | 11 |
| `expand_macro` | 442 |
| `expand_text` | 120 |

§6.10.3.1's argument prescan is a **recursion the input drives** —
`expand_macro` → `expand_text` → `scan_token` → `expand_macro` — and it is the only one in this
program. Resident before any expansion: 41 + 372 + 656 + 11 = **1,080 words**. Each further level
costs `expand_macro` + `expand_text` + `scan_token` + `lookup_token` = **1,227**, and the inner
macro's *argument collection* re-enters `scan_token` for another **1,106** before `expand_text`
is even reached. So one level fits at ≈3,400 of the 4,096 and two do not — hence
`MAXARGDEPTH 1`.

**What that costs.** Past the bound the argument is substituted **raw** instead of pre-expanded,
with a warning (`-w` silences it) and **not** an error: the substituted text is rescanned in the
ordinary way afterwards, so for everything but a `#`/`##` operand — which takes the raw actual
regardless — the result is the same text. `MAX(1, MIN(2,3))` and `CLAMP(5,0,9)` come out
byte-identical to the host, and `rootfs/test/deep.c` asserts exactly that. **The one shape that
genuinely diverges** is a macro nested inside *its own* argument two deep, `ID(ID(ID(4)))`: the
raw text is rescanned with the outer `ID` still blue-painted (§6.10.3.4), so the inner call is
left un-expanded where the host expands it. `cmd_cpp_nesting` records that difference as a fact.

**This bound is the stack's, not the preprocessor's.** Raising `USTKPAGE` from 28 to 24
(`include/sys/param.h`) would give 8,192 words of stack and 24,576 of image. That is a kernel ABI
change and belongs to kernel task 39, not here — and **it is
no longer free.** When this was written every program on the disk fitted under 24,576; task C9b
has since put `/usr/bin/ld` on it at **23,951 words**, whose ~4,700 words of headroom are the heap
budget for twelve stdio buffers. Cutting the ceiling to 24,576 would leave it 625 and stop it
linking. `cpp` is the only program that would gain, and `as` and `ld` — which that task expected
to want the bigger stack too — turned out not to ([../ld/README.md](../ld/README.md)).

### Testing the native build

Two kinds, both in [`rootfs/test/`](rootfs/test):

- **`rootfs_cpp_agree` / `rootfs_cpp_deep`** — the host `b6cpp` and the native `cpp` over one
  fixture, diffed **live**. Two cpps built from one source must agree byte for byte; a
  checked-in expectation could not express that, because the day a `##` corner is fixed in
  `macro.c` a live diff *requires* the fix to land identically on both targets.
- **`cmd_cpp_*`** — ordinary `b6sim` cases with a checked-in `.expected`, which is the other
  half: they pin the output and the exit status, which two cpps wrong in the same way would not.

`rootfs_cpp_size` (registered by `b6_prog()`) is the third: it holds the program under 28,672
words and its top symbol under 32,767. Today it links at **23,826** words, leaving ~4,800 for the
heap — which is not slack but budget, since `malloc` grows a page at a time and one expansion
level takes two pages.

The stronger check, by hand, is the real workload:

```sh
build/cmd/cpp/b6cpp -Iinclude -I~/.local/share/besm6/include kernel/sys1.c > a
b6sim build/rootfs/usr/bin/cpp -Iinclude -I~/.local/share/besm6/include kernel/sys1.c > b
diff a b
```

`kernel/sys1.c`, `kernel/main.c`, `kernel/dev/tty.c`, `cmd/sh/xec.c`, `cmd/sed/sed0.c`,
`lib/libc/gen/malloc.c` and `cmd/cpp/macro.c` itself all come out identical.

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
