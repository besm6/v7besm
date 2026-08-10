# `cmd/yacc` — the parser generator

Task **C10a** ([`../TODO.md`](../TODO.md)): the host tool `b6yacc`, the parser skeleton it
copies, and the `b6_yacc()` CMake helper. C10b builds `b6lex` on top of it; C10c puts the same
sources on the image as `/usr/bin/yacc`.

**This directory is the first exception to [`../README.md`](../README.md)'s "the sources are
already here".** Everything else under [`../`](../) came from an unpacked v7 reference tree;
`y1.c`, `y2.c`, `y3.c`, `y4.c`, `dextern.h` and `yaccpar.c` were fetched from RetroBSD
(`src/cmd/yacc`), which is 4.2BSD's yacc with the ANSI pass already done — prototypes
throughout, `<stdarg.h>` for `error()`, no K&R parameter list anywhere. The manual page is v7's,
converted with `b6man2umm`; RetroBSD keeps it in a central `man/` tree rather than here.

## What the port had to change

Most of §1 was already done. What was left is below, and each item is a decision rather than a
transcription.

### Sizes

`dextern.h` carried three size profiles — `HUGE`, `MEDIUM`, `TINY` — selected by `#define`ing
one of them in a second header, `files.h`. **The shipped selection was `TINY`, and `TINY`
violates an invariant the header itself states two lines below the table**: `TEMPSIZE` 200
against `NSTATES` 750. `temp1[TEMPSIZE]` is indexed by state number in `others()` and
`callopt()`, so any grammar over 200 states wrote past the end of it in silence. `MEDIUM` and
`HUGE` both hold.

Three profiles of which one is broken, one untested and one selected by editing a second header
is not a mechanism worth keeping, so:

* the numbers are now **one block**, `HUGE`'s, in `dextern.h`, to be keyed on `besm6` when C10c
  adds that arm — the shape `cmd/cpp`, `cmd/as` and `cmd/ld` already use;
* `files.h` is gone, having held nothing else;
* the invariants are **`_Static_assert`s**, §12's habit turned on the program's own arithmetic.
  There are six, one more than the header's comment listed: `temp1[]` is also indexed by
  production number in `others()`, and `TEMPSIZE >= NPROD` was nowhere written down.

`HUGE` is right for the host on capability rather than on memory: `NTERMS` is 300 there and 127
in the other two, and **300 is the only one of the three in which a token value above 127 is
representable at all** (§11). Table capacity does not enter the generated output, so C10c's
smaller `besm6` profile will still agree byte for byte on any grammar that fits both.

`WORD32`, the 32-bit lookahead-set alternative, is **deleted rather than left unselected**: its
bit-packing macros shift by 31, and an `int` is 41 bits on the target, so the option was never
available to this program. It bought four words per lookahead set.

### The skeleton

`yaccpar.c` is **data to this tool and source to its caller** — `others()` in `y1.c` copies it
into `y.tab.c` a byte at a time — so it is compiled by `b6cc` for a native program and by the
host compiler for `cmd/lex`. It was the least modernised file in the tree and **nothing in
C10b–C17 compiles until it is right**:

* `yyparse()` had no return type and an empty parameter list.
* `yylex()` and `yyerror()` were called with **no declaration anywhere**, which C11 refuses.
* `yyps = &yys[-1]` and `yypv = &yyv[-1]` formed pointers below the base of their own arrays —
  §2's third hazard, and the error-recovery loop popped down to them. Both stacks carry one
  extra slot now and index 0 is a sentinel; the usable depth is unchanged.

### The contract

The two prototypes could not be added to `yaccpar.c`. `y.tab.c` is emitted as *the user's
declarations, the generated header block, the tables, the user's own subroutines, and only then
the skeleton*, so a declaration inside the skeleton would follow the definitions it describes
and collide with them. `y2.c` emits them with the header block instead, which makes them a
contract on every grammar in this tree:

```c
int  yylex(void);
void yyerror(char *);
```

C10b's `ncform` and each of C11–C17's grammars must define both with these signatures.

**The action marker may appear in the skeleton exactly once, comments included.** `others()`
splices the action stream at the first one it sees and closes the stream behind it; a second
occurrence — in a comment, which is how this was found — reached a closed stream. It is a
diagnostic now rather than a null `FILE *`.

### Temporary files

`yacc.tmp` and `yacc.acts` were fixed names in the working directory, written in one pass,
closed, and **reopened by name** in the next. That is what the manual page's BUGS paragraph was
about, and it left both files behind on every error path. They are `tmpfile()` streams now,
rewound rather than reopened, so the handles are threaded through `setup()` → `summary()` →
`callopt()` and `finact()` → `others()`.

The three **output** names stay fixed — `y.tab.c`, `y.tab.h` and `y.output` are the interface —
which is why `b6_yacc()` still gives every grammar a working directory of its own.

### Finding the skeleton

`files.h` hardcoded `/usr/share/misc/yaccpar.c` and **RetroBSD's makefile installs the template
nowhere at all**, so the tool as shipped could not run. `find_parser()` in `y1.c` is a path
profile on `cc.c`'s model:

1. `$B6YACCPAR`, if set and non-empty — **not** keyed on `besm6`, exactly as `cc.c`'s tool
   overrides are not: it is how a test, and `b6_yacc()`, reach a skeleton that has not been
   installed;
2. `/usr/lib/yaccpar` on the machine itself (C10c's location);
3. `~/.local/share/besm6/yaccpar.c`, then `/usr/local/share/besm6/yaccpar.c`, on the host.

It returns the last candidate when none exists, so the diagnostic names a path a user can act
on.

### `y4`'s borrowed arrays

The optimizer renames five of the generator's arrays (`amem`, `mem0`, `indgo`, `temp1`,
`tystate`) under the names pass 2 thinks in. That is harmless — each names the same array.

Three more were not:

```c
int *ggreed = lkst[0].lset;
int *pgo    = wsets[0].ws.lset;
int *yypgo  = &nontrst[0].tvalue;
```

Nothing in `y4` wants a lookahead set or a nonterminal's name; it wants the dead storage they
sit in, and it walks each as a flat `int` vector far past the member it points at. The
arithmetic that makes them fit assumes a pointer and an `int` are the same width — `yypgo[i]`
strides one `int` through an array of `struct ntsymb`. True here and on the target, and §2 says
to grep for exactly this shape rather than leave it because it works. They are three declared
arrays now, `NNONTERM + 2` words each.

## Building

`b6yacc` is a host tool: `add_executable`, `install(TARGETS)`, and one `install(FILES)` putting
`yaccpar.c` beside the header tree at `<prefix>/share/besm6/`.

**`b6_yacc()`** ([`../../scripts/BesmCross.cmake`](../../scripts/BesmCross.cmake)) runs the
in-tree `b6yacc` over a `.y` at build time and hands the result to `b6_obj()`, so a rebuilt
`b6yacc` regenerates every parser with no `make install`. Three things are load-bearing:

* **a working directory per grammar**, `<name>.yacc.dir`, because the three output names are
  fixed and a shared directory races under `make -j` for the same reason `kernel/test`'s
  per-program object directories do;
* **the generated source is renamed** `y.tab.c` → `<name>.c`, because CMake's `NAME_WE` cuts
  `y.tab.c` at the first dot and every grammar in the tree would otherwise compile to `y.o`;
* **`b6yacc` and `yaccpar.c` are both in `DEPENDS`.** A `$<TARGET_FILE:>` in the `COMMAND` alone
  buys ordering, not staleness — the Makefile generator emits no file prerequisite for it — and
  the skeleton is a data file no target-level dependency could see at all. Editing either
  regenerates every parser now, which is what the no-`make install` claim actually needs.

It lives inside the `libruntime` guard and so serves the cross builds only. C10b's `b6lex` is a
*host* program built from a generated parser and will name `$<TARGET_FILE:b6yacc>` in a custom
command of its own.

## Tests

**`test/`** is a GoogleTest suite driving the binary as a subprocess (`cmd/cpp/test`'s shape;
there is no engine library to link). Three kinds:

* what the tool refuses and what it emits, including the two prototypes above and the assertion
  that **no temporary files are left in the working directory**;
* `b6yacc` over the six grammars C11–C17 will feed it, which are already sitting under `cmd/`.
  All six generate. The conflict counts are pinned from a measured run:

  | grammar | shift/reduce | reduce/reduce |
  |---|---|---|
  | `expr/expr.y` | 0 | 0 |
  | `egrep/egrep.y` | 2 | 0 |
  | `m4/m4y.y` | 0 | 0 |
  | `make/gram.y` | 0 | 0 |
  | `bc/bc.y` | 12 | 30 |
  | `awk/awk.g.y` | 95 | 0 |

  `bc` and `awk` are as v7 wrote them; the numbers are recorded so that a change in either the
  generator or the grammar is a stop rather than a shrug.
* a generated parser **compiled by a C compiler and run**, at `-std=c11 -pedantic-errors -Wall
  -Werror`, which is the only host-side proof that the skeleton produces code. Two warnings are
  turned off there and they are properties of yacc's output rather than of this skeleton:
  `yyerrlab` is unreferenced unless the grammar uses `YYERROR`, and `yypvt` is set but unused
  unless an action names `$n`.

**The host's own `yacc` is not an oracle.** It is bison in compatibility mode on this machine
and its tables share nothing with these, so there is no byte comparison to make — only conflict
counts and token numbering, which is a reading exercise done by hand. Depending on bison would
also spend the property that this build needs nothing but CMake and a C++17 compiler, which is
the reason `cmd/yacc` exists at all.

### The native case

**`rootfs/calct.y`** is a desk calculator, built through `b6_yacc()` + `b6_prog()` and run under
`b6sim` from `build/rootfs/test/`. It is **not on the image** — `root.manifest` names every file
explicitly — and it is the only thing in C10a that says **`b6parse` accepts the rewritten
skeleton** and that a generated parser parses on a BESM-6. It also gives `b6_yacc()` a caller
instead of leaving it untested until C10b.

Its four cases exercise what the port rewrote: arithmetic (a reduction stack deeper than one),
precedence, `error` recovery (which pops to the new sentinel), and eight-bit input. The last is
an assertion rather than an eyeball: `Ы` is `D0 AB`, `0253` masked to seven bits is `+`, and the
case reports the offending byte **by value**, so anything that masked would print 80 and
evaluate the line instead of refusing it.

`rootfs/` rather than `test/` because `cmd/yacc` is added to the build outside the `libruntime`
guard, where `b6_prog()` does not exist — `cmd/libaout/rootfs` is the same shape, and C10c adds
`/usr/bin/yacc` to this directory beside it.

## Traps

* **Do not run `clang-format` over `yaccpar.c` casually.** It is not valid C — the action marker
  stands where a `switch` body belongs — and a reformat that moves it changes the generated
  output.
* `short` is `int` is **one word** on the target, so a generated parser's `short yys[YYMAXDEPTH]`
  costs 150 words of stack and `YYSTYPE yyv[YYMAXDEPTH]` 150 of bss before any table. That is the
  fixed price every C11–C17 program inherits. `calct` measures it: **4,903 words** — 82 const,
  3,144 text, 486 data, 1,191 bss — for a six-rule grammar over stdio, against the 28,672-word
  ceiling. Nearly all of it is stdio; §6's note that what a program prints with dominates what it
  does holds here too.
