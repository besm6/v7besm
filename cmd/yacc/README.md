# `cmd/yacc` — the parser generator

Tasks **C10a** and **C10c** ([`../README.md`](../README.md)): the host tool `b6yacc`, the parser
skeleton it copies, and the `b6_yacc()` CMake helper — then the same sources built a second time
as the machine's own `/usr/bin/yacc`. C10b builds `b6lex` on top of the first.

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

* the numbers are now **one block keyed on `besm6`**, `HUGE`'s on the host — the shape `cmd/cpp`,
  `cmd/as` and `cmd/ld` already use. The target arm is C10c's and is below;
* `files.h` is gone, having held nothing else;
* the invariants are **`_Static_assert`s**, §12's habit turned on the program's own arithmetic.
  There are six, one more than the header's comment listed: `temp1[]` is also indexed by
  production number in `others()`, and `TEMPSIZE >= NPROD` was nowhere written down.

`HUGE` is right for the host on capability rather than on memory: `NTERMS` is 300 there and 127
in the other two, and **300 is the only one of the three in which a token value above 127 is
representable at all** (§11). Table capacity does not enter the generated output, which is what
makes C10c's smaller profile safe — and is now measured rather than argued.

`WORD32`, the 32-bit lookahead-set alternative, is **deleted rather than left unselected**: its
bit-packing macros shift by 31, and an `int` is 41 bits on the target, so the option was never
available to this program. It bought four words per lookahead set.

### The skeleton

`yaccpar.c` is **data to this tool and source to its caller** — `others()` in `y1.c` copies it
into `y.tab.c` a byte at a time — so it is compiled by `b6cc` for a native program and by the
host compiler for `cmd/lex`. It was the least modernised file in the tree and **nothing in
C10b–C17 compiles until it is right**; C10b is the first proof that it is:

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

C10b's `ncform` does, and each of C11–C17's grammars must define both with these signatures.
C13 is the one that found the sharp edge: v7's `m4y.y` wrote `yyerror(s) char *s; {}`, which is
`int` by omission, and the emitted `void` prototype makes that a **hard error** rather than the
silent mismatch a PDP-11 linker allowed.

**And `YYSTYPE` is a macro, not a type name**, which is the other half of the same contract.
`y2.c` emits `YYSTYPE yylval, yyval;`, so a grammar whose value type is a pointer must spell it
through a `typedef` — `#define YYSTYPE char *` declares `yyval` a plain `char` and diagnoses
nothing. C11 is the worked example ([`../expr/README.md`](../expr/README.md)), and it also
records why `%union` was not the answer there.

**The `%union` path is exercised**, by [`rootfs/calcu.y`](rootfs/calcu.y) — `calct` again over
`%union { int i; char *s; }` with both tags used. It exists because declaring a union changes
what the skeleton *does*: `YYSTYPE` stops being a word and the three value copies in
[`yaccpar.c`](yaccpar.c) — `*yypv = yyval` on a push, `yyval = yylval` on a shift, and
`yyval = yypv[1]` for a rule with no action — become aggregate copies, compiled by `b6lower`'s
`gen_aggregate_assign()` rather than moved in one instruction. Nothing in this tree had ever
run one, and C14's [`../make/gram.y`](../make/gram.y) — still the only `%union` in a real
program here — would have been the first, where a miscompile would have read as a grammar bug.
Hence a grammar of its own, and hence the `char *` tag: a fat pointer is the member likeliest
to survive a word move and not an aggregate one. `list: list stat '\n'` carries **no action**
deliberately; that is the third copy. Three `b6_progtest` cases and a `yacc_agree` line, and
`gram.y` was already among the agreement grammars.
[`../lex/README.md`](../lex/README.md) under "The contract" has nine of its own in the other
direction — and one warning worth reading here: `FILE *yyin ={stdin}` is **not a constant
initialiser**, which is what stopped every scanner v7's lex generated from compiling. `yaccpar.c`
is worth re-reading for the same shape.

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
2. `/usr/lib/yaccpar` on the machine itself, which is where C10c staged it;
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
*host* program built from a generated parser and names `$<TARGET_FILE:b6yacc>` in a custom
command of its own ([`../lex/CMakeLists.txt`](../lex/CMakeLists.txt)) — the first consumer of
this tool that is not a test, and the first host one. `b6_lex()` sits beside `b6_yacc()` now.

## Building for the BESM-6

These same four sources are built a **second** time, by the `b6*` cross toolchain, into
`build/rootfs/usr/bin/yacc` — the parser generator that runs on the machine, and the twelfth
program of a toolchain that was eleven (task **C10c**). [`rootfs/CMakeLists.txt`](rootfs) is the
whole of the build machinery, there is **no second copy of any source**, and the skeleton is
staged beside it as `/usr/lib/yaccpar` because `find_parser()`'s `besm6` arm names that path and
has no second candidate. What differs is a size profile in [`dextern.h`](dextern.h) keyed on the
`besm6` predefine, and a stdio buffer.

**At the host sizes the program does not fit at all**: 79,226 words — 145 const, 10,515 text,
1,753 data, **66,813 bss** — against the 28,672 a user program gets for everything including its
heap. bss is the whole of the problem; text does not move with the profile.

The numbers are measured from the six grammars C11–C17 will feed this yacc, which are the only
input it has to accept. `awk.g.y` is the worst of them on every count but one, and `bc.y` peaks
the working sets:

| | host | BESM-6 | measured need | why |
| --- | ---: | ---: | ---: | --- |
| `ACTSIZE` | 12000 | 2200 | 1727 | the optimizer's output table, one word each |
| `MEMSIZE` | 24000 | 4800 | 3660 | production and optimizer storage; the largest single array |
| `NSTATES` | 750 | 320 | 242 | five words a state across `pstate`/`tystate`/`indgo`/`mstates`/`defact` |
| `NTERMS` | 300 | 144 | 95 | **not cut to the measurement** — see below |
| `NPROD` | 600 | 200 | 122 | three words a rule |
| `NNONTERM` | 300 | 48 | 30 | nine words a nonterminal, the dearest unit here |
| `TEMPSIZE` | 1200 | 320 | — | the asserts fix it: `>= NSTATES`, `>= NPROD`, `>= NTERMS+NNONTERM+1` |
| `CNAMSZ` | 5000 | 2400 | — | the name arena, six chars to a word |
| `LSETSIZE` | 600 | 128 | 98 | `TBITSET` words a set, so it scales with `NTERMS` too |
| `WSETSIZE` | 350 | 112 | 88 | `TBITSET + 2` words each, likewise |

**`NTERMS` is the one not cut to what the grammars use.** It caps the number of *distinct*
terminals and a character literal is one of them, so a value below 128 would stop a grammar using
eight-bit literals at all — a capability limit rather than a memory one (§11). It is also charged
twice over, `TBITSET` growing with it and every `struct looksets` and `struct wset` carrying a
set: 144 keeps `TBITSET` at 10 where 256 would make it 17 and cost some 1,200 words.

**The stdio buffer is not optional either.** `yacc` calls no `malloc` anywhere, so its entire
heap is stdio's — and it holds seven streams open at once: the grammar, `y.tab.c`, `y.tab.h`
under `-d`, `y.output` under `-v`, the two `tmpfile()`s and `stdout`. At the default `BUFSIZ`,
3,072 bytes or 512 words, that is 3,584 words against the ~2,000 the profile leaves;
`rootfs_yacc_size` cannot see a byte of it, so the program would link and a real run would die.
`YYBUFSIZ` is 1026 bytes — 171 words, and a whole number of them, as
[`../ld/intern.h`](../ld/intern.h)'s `LDBUFSIZ` explains — so the same seven cost about 1,200.

It links at **26,647 words**: 143 const, 10,629 text, 1,353 data, 14,522 bss, top symbol
`064037`. On `awk.g.y`, the largest grammar it will ever be given, every bound comes back with
headroom — `3660/4800` of memory, `1727/2200` of action table, `242/320` states, `98/128`
lookahead sets. The largest stack frame is **434 words** of 4,096 and the only automatic array
in the program is a `char actname[8]`, so §6's stack ceiling does not come near. On the disk it
costs 30 blocks of the 460 that were free: 26 for the binary, 2 for the skeleton, 2 for the page.

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
  -Werror`, which is the only host-side proof that the skeleton produces code. `cmd/lex/test` is
  the second place that happens, and `parser.y` is the seventh grammar this yacc is held to. Two warnings are
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
guard, where `b6_prog()` does not exist — `cmd/libaout/rootfs` is the same shape. C10c's
`/usr/bin/yacc` is in that directory beside it.

### Testing the native build

Two kinds, both in [`rootfs/test/`](rootfs/test):

* **`rootfs_yacc_*`** — the host `b6yacc` and the native `yacc` over one grammar, `y.tab.c`,
  `y.tab.h` and the message stream diffed **live**. Two yaccs built from one source must generate
  the same parser, and a checked-in expectation cannot say that: the day a table is emitted
  differently in `y3.c`, a live diff *requires* the change to land identically on both targets.
  There are eight cases and each is a real grammar — the six of C11–C17, `lex/parser.y` (which is
  the machine generating a real tool's parser) and `calct.y`. A grammar written for this suite
  could be sized to fit and would prove only that it fits.
* **`cmd_yacc_*`** — ordinary `b6sim` cases with a checked-in `.expected`, which is the other
  half: they pin the diagnostics and the exit status, which two yaccs wrong in the same way would
  not.

**`y.output` is the one output the two builds cannot agree on**, and it is deliberately not
compared: its statistics section quotes the profile's own bounds — `95/300 terminals` on the host
against `95/144` here. Nothing else does. In particular no message carries `argv[0]` — `error()`
prints `" fatal error: "` and nothing more — so the message stream *is* comparable across the two
worlds, which is unusual here and is why the conflict counts are part of the live diff rather
than a separate assertion.

**Both sides are told where the skeleton is.** The host's search path ends at an installed
`share/besm6/yaccpar.c` and the native one is `/usr/lib/yaccpar`, which under `b6sim` is the
*build machine's*; `$B6YACCPAR` is checked ahead of either, and `ENV_WHITELIST` in
[`../sim/session.cpp`](../sim/session.cpp) passes it to the guest for this test's sake and
C10d's. The `cmd_yacc_*` cases run under `env -i` and so cannot see it, which is why every one of
them is a case that fails *before* `others()` wants the skeleton.

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
